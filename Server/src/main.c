#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "model_params.h"
#include "semg_features.h"
#include "tflite_weights.h"
#include <mosquitto.h>

#define MQTT_DEFAULT_HOST "127.0.0.1"
#define MQTT_DEFAULT_PORT 1883
#define HTTP_DEFAULT_PORT 8080
#define MQTT_KEEPALIVE_SEC 60

#define MQTT_TOPIC_SUB_FEAT "semg/v1/+/features"
#define MQTT_TOPIC_SUB_ENDDEV "semg/v1/end_device/+/status"
#define MQTT_TOPIC_PUB_CMD "semg/v1/control/gesture"

#define MAX_DEVICES 16
#define MAX_VOTE_WINDOW 10
#define MAX_LOG_ENTRIES 20
#define DEVICE_TIMEOUT_SEC 10

typedef struct TfLiteModel TfLiteModel;
typedef struct TfLiteInterpreterOptions TfLiteInterpreterOptions;
typedef struct TfLiteInterpreter TfLiteInterpreter;
typedef struct TfLiteTensor TfLiteTensor;
typedef enum
{
    kTfLiteOk    = 0,
    kTfLiteError = 1
} TfLiteStatus;

typedef TfLiteModel* (*pfn_TfLiteModelCreateFromFile)(const char*);
typedef void (*pfn_TfLiteModelDelete)(TfLiteModel*);
typedef TfLiteInterpreterOptions* (*pfn_TfLiteInterpreterOptionsCreate)(void);
typedef void (*pfn_TfLiteInterpreterOptionsDelete)(TfLiteInterpreterOptions*);
typedef TfLiteInterpreter* (*pfn_TfLiteInterpreterCreate)(const TfLiteModel*, const TfLiteInterpreterOptions*);
typedef void (*pfn_TfLiteInterpreterDelete)(TfLiteInterpreter*);
typedef TfLiteStatus (*pfn_TfLiteInterpreterAllocateTensors)(TfLiteInterpreter*);
typedef TfLiteTensor* (*pfn_TfLiteInterpreterGetInputTensor)(const TfLiteInterpreter*, int32_t);
typedef const TfLiteTensor* (*pfn_TfLiteInterpreterGetOutputTensor)(const TfLiteInterpreter*, int32_t);
typedef TfLiteStatus (*pfn_TfLiteTensorCopyFromBuffer)(TfLiteTensor*, const void*, size_t);
typedef TfLiteStatus (*pfn_TfLiteInterpreterInvoke)(TfLiteInterpreter*);
typedef TfLiteStatus (*pfn_TfLiteTensorCopyToBuffer)(const TfLiteTensor*, void*, size_t);

typedef struct
{
    bool use_dll;
#ifdef _WIN32
    HMODULE dll_handle;
#endif
    TfLiteModel* model;
    TfLiteInterpreterOptions* options;
    TfLiteInterpreter* interpreter;
    TfLiteTensor* input_tensor;
    const TfLiteTensor* output_tensor;
    pfn_TfLiteModelDelete fn_model_delete;
    pfn_TfLiteInterpreterOptionsDelete fn_options_delete;
    pfn_TfLiteInterpreterDelete fn_interp_delete;
    pfn_TfLiteTensorCopyFromBuffer fn_copy_from;
    pfn_TfLiteInterpreterInvoke fn_invoke;
    pfn_TfLiteTensorCopyToBuffer fn_copy_to;
} tflite_runtime_t;

typedef enum
{
    DEV_ROLE_SENSOR     = 0,
    DEV_ROLE_END_DEVICE = 1
} dev_role_t;

typedef struct
{
    bool active;
    char id[48];
    dev_role_t role;
    time_t last_seen;
    uint32_t packet_count;
    int last_seq;
    char last_status[64];
} tracked_device_t;

typedef struct
{
    int seq;
    char device_id[48];
    int raw_gesture;
    int stable_gesture;
    float confidence;
    char timestamp[16];
} log_entry_t;

typedef struct
{
    tflite_runtime_t tflite;
    struct mosquitto* mosq;
    bool mqtt_connected;
    time_t start_time;
    uint32_t total_rx_packets;
    uint32_t total_tx_commands;

    /* Tunable runtime settings from GUI */
    float conf_threshold;
    int vote_window;
    char gesture_actions[SEMG_NUM_CLASSES][48];

    /* Latest inference data */
    int last_seq;
    char last_sensor_id[48];
    float raw_features[SEMG_TOTAL_FEATURES];
    float norm_features[SEMG_TOTAL_FEATURES];
    float probs[SEMG_NUM_CLASSES];
    int current_raw_gesture;
    float current_conf;
    int last_published_gesture;

    int vote_buf[MAX_VOTE_WINDOW];
    int vote_idx;
    int vote_count;

    tracked_device_t devices[MAX_DEVICES];
    log_entry_t logs[MAX_LOG_ENTRIES];
    int log_head;
    int log_count;
} server_ctx_t;

static volatile sig_atomic_t g_running = 1;
static server_ctx_t g_ctx;

static void handle_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

static void format_time_now(char* out_buf, size_t max_len)
{
    time_t now         = time(NULL);
    struct tm* tm_info = localtime(&now);
    if (tm_info != NULL)
    {
        strftime(out_buf, max_len, "%H:%M:%S", tm_info);
    }
    else
    {
        snprintf(out_buf, max_len, "00:00:00");
    }
}

static tracked_device_t*
upsert_device(server_ctx_t* ctx, const char* dev_id, dev_role_t role, int seq, const char* status_str)
{
    time_t now    = time(NULL);
    int free_slot = -1;

    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (ctx->devices[i].active && strcmp(ctx->devices[i].id, dev_id) == 0 && ctx->devices[i].role == role)
        {
            ctx->devices[i].last_seen = now;
            ctx->devices[i].packet_count++;
            ctx->devices[i].last_seq = seq;
            if (status_str != NULL)
            {
                snprintf(ctx->devices[i].last_status, sizeof(ctx->devices[i].last_status), "%s", status_str);
            }
            return &ctx->devices[i];
        }
        if (!ctx->devices[i].active && free_slot == -1)
        {
            free_slot = i;
        }
    }

    if (free_slot != -1)
    {
        tracked_device_t* d = &ctx->devices[free_slot];
        memset(d, 0, sizeof(*d));
        d->active = true;
        snprintf(d->id, sizeof(d->id), "%s", dev_id);
        d->role         = role;
        d->last_seen    = now;
        d->packet_count = 1;
        d->last_seq     = seq;
        snprintf(d->last_status, sizeof(d->last_status), "%s", status_str ? status_str : "Active");
        return d;
    }
    return NULL;
}

static void push_log(server_ctx_t* ctx, const char* dev_id, int seq, int raw_cls, int stable_cls, float conf)
{
    log_entry_t* entry = &ctx->logs[ctx->log_head];
    entry->seq         = seq;
    snprintf(entry->device_id, sizeof(entry->device_id), "%s", dev_id);
    entry->raw_gesture    = raw_cls;
    entry->stable_gesture = stable_cls;
    entry->confidence     = conf;
    format_time_now(entry->timestamp, sizeof(entry->timestamp));

    ctx->log_head = (ctx->log_head + 1) % MAX_LOG_ENTRIES;
    if (ctx->log_count < MAX_LOG_ENTRIES)
    {
        ctx->log_count++;
    }
}

static void mlp_infer_builtin(const float in_norm[SEMG_TOTAL_FEATURES], float out_probs[SEMG_NUM_CLASSES])
{
    float h1[64];
    float h2[32];
    float logits[SEMG_NUM_CLASSES];

    for (int i = 0; i < 64; i++)
    {
        float acc          = MLP_B1[i];
        const float* w_row = &MLP_W1[i * 15];
        for (int j = 0; j < 15; j++)
        {
            acc += w_row[j] * in_norm[j];
        }
        h1[i] = (acc > 0.0f) ? acc : 0.0f;
    }

    for (int i = 0; i < 32; i++)
    {
        float acc          = MLP_B2[i];
        const float* w_row = &MLP_W2[i * 64];
        for (int j = 0; j < 64; j++)
        {
            acc += w_row[j] * h1[j];
        }
        h2[i] = (acc > 0.0f) ? acc : 0.0f;
    }

    float max_logit = -1e30f;
    for (int i = 0; i < SEMG_NUM_CLASSES; i++)
    {
        float acc          = MLP_B3[i];
        const float* w_row = &MLP_W3[i * 32];
        for (int j = 0; j < 32; j++)
        {
            acc += w_row[j] * h2[j];
        }
        logits[i] = acc;
        if (acc > max_logit)
        {
            max_logit = acc;
        }
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < SEMG_NUM_CLASSES; i++)
    {
        out_probs[i] = expf(logits[i] - max_logit);
        sum_exp += out_probs[i];
    }
    for (int i = 0; i < SEMG_NUM_CLASSES; i++)
    {
        out_probs[i] /= sum_exp;
    }
}

static int ml_runtime_init(tflite_runtime_t* rt, const char* model_path)
{
    if (rt == NULL)
        return -1;
    memset(rt, 0, sizeof(*rt));

#ifdef _WIN32
    rt->dll_handle = LoadLibraryA("tensorflowlite_c.dll");
    if (rt->dll_handle != NULL && model_path != NULL)
    {
        pfn_TfLiteModelCreateFromFile fn_model_create =
            (pfn_TfLiteModelCreateFromFile)(void*)GetProcAddress(rt->dll_handle, "TfLiteModelCreateFromFile");
        pfn_TfLiteInterpreterOptionsCreate fn_opt_create =
            (pfn_TfLiteInterpreterOptionsCreate)(void*)GetProcAddress(rt->dll_handle, "TfLiteInterpreterOptionsCreate");
        pfn_TfLiteInterpreterCreate fn_interp_create =
            (pfn_TfLiteInterpreterCreate)(void*)GetProcAddress(rt->dll_handle, "TfLiteInterpreterCreate");
        pfn_TfLiteInterpreterAllocateTensors fn_alloc = (pfn_TfLiteInterpreterAllocateTensors)(void*)GetProcAddress(
            rt->dll_handle, "TfLiteInterpreterAllocateTensors");
        pfn_TfLiteInterpreterGetInputTensor fn_get_in = (pfn_TfLiteInterpreterGetInputTensor)(void*)GetProcAddress(
            rt->dll_handle, "TfLiteInterpreterGetInputTensor");
        pfn_TfLiteInterpreterGetOutputTensor fn_get_out = (pfn_TfLiteInterpreterGetOutputTensor)(void*)GetProcAddress(
            rt->dll_handle, "TfLiteInterpreterGetOutputTensor");

        rt->fn_model_delete = (pfn_TfLiteModelDelete)(void*)GetProcAddress(rt->dll_handle, "TfLiteModelDelete");
        rt->fn_options_delete =
            (pfn_TfLiteInterpreterOptionsDelete)(void*)GetProcAddress(rt->dll_handle, "TfLiteInterpreterOptionsDelete");
        rt->fn_interp_delete =
            (pfn_TfLiteInterpreterDelete)(void*)GetProcAddress(rt->dll_handle, "TfLiteInterpreterDelete");
        rt->fn_copy_from =
            (pfn_TfLiteTensorCopyFromBuffer)(void*)GetProcAddress(rt->dll_handle, "TfLiteTensorCopyFromBuffer");
        rt->fn_invoke = (pfn_TfLiteInterpreterInvoke)(void*)GetProcAddress(rt->dll_handle, "TfLiteInterpreterInvoke");
        rt->fn_copy_to =
            (pfn_TfLiteTensorCopyToBuffer)(void*)GetProcAddress(rt->dll_handle, "TfLiteTensorCopyToBuffer");

        if (fn_model_create && fn_opt_create && fn_interp_create && fn_alloc && fn_get_in && fn_get_out &&
            rt->fn_copy_from && rt->fn_invoke && rt->fn_copy_to)
        {
            rt->model = fn_model_create(model_path);
            if (rt->model != NULL)
            {
                rt->options     = fn_opt_create();
                rt->interpreter = fn_interp_create(rt->model, rt->options);
                if (rt->interpreter != NULL && fn_alloc(rt->interpreter) == kTfLiteOk)
                {
                    rt->input_tensor  = fn_get_in(rt->interpreter, 0);
                    rt->output_tensor = fn_get_out(rt->interpreter, 0);
                    if (rt->input_tensor != NULL && rt->output_tensor != NULL)
                    {
                        rt->use_dll = true;
                        printf("[ML] Loaded tensorflowlite_c.dll + %s successfully.\n", model_path);
                        return 0;
                    }
                }
            }
        }
        FreeLibrary(rt->dll_handle);
        rt->dll_handle = NULL;
    }
#else
    (void)model_path;
#endif

    rt->use_dll = false;
    printf("[ML] Using built-in C TFLite weights engine (semg_mlp.tflite embedded tensors).\n");
    return 0;
}

static int
ml_runtime_infer(tflite_runtime_t* rt, const float in_norm[SEMG_TOTAL_FEATURES], float out_probs[SEMG_NUM_CLASSES])
{
    if (rt == NULL || in_norm == NULL || out_probs == NULL)
        return -1;

    if (rt->use_dll)
    {
        if (rt->fn_copy_from(rt->input_tensor, in_norm, sizeof(float) * SEMG_TOTAL_FEATURES) != kTfLiteOk)
            return -2;
        if (rt->fn_invoke(rt->interpreter) != kTfLiteOk)
            return -3;
        if (rt->fn_copy_to(rt->output_tensor, out_probs, sizeof(float) * SEMG_NUM_CLASSES) != kTfLiteOk)
            return -4;
        return 0;
    }

    mlp_infer_builtin(in_norm, out_probs);
    return 0;
}

static void ml_runtime_deinit(tflite_runtime_t* rt)
{
    if (rt == NULL)
        return;
    if (rt->use_dll)
    {
        if (rt->interpreter && rt->fn_interp_delete)
            rt->fn_interp_delete(rt->interpreter);
        if (rt->options && rt->fn_options_delete)
            rt->fn_options_delete(rt->options);
        if (rt->model && rt->fn_model_delete)
            rt->fn_model_delete(rt->model);
#ifdef _WIN32
        if (rt->dll_handle)
            FreeLibrary(rt->dll_handle);
#endif
    }
}

static int parse_feature_payload(const char* payload, int payload_len, int* out_seq, float out_f[SEMG_TOTAL_FEATURES])
{
    if (payload == NULL || payload_len <= 0 || out_seq == NULL || out_f == NULL)
        return -1;

    char buf[1024];
    int copy_len = (payload_len < (int)sizeof(buf) - 1) ? payload_len : ((int)sizeof(buf) - 1);
    memcpy(buf, payload, (size_t)copy_len);
    buf[copy_len] = '\0';

    *out_seq            = 0;
    const char* seq_pos = strstr(buf, "\"seq\"");
    if (seq_pos != NULL)
    {
        const char* colon = strchr(seq_pos, ':');
        if (colon != NULL)
        {
            *out_seq = (int)strtol(colon + 1, NULL, 10);
        }
    }

    const char* bracket = strchr(buf, '[');
    if (bracket == NULL)
        return -2;

    const char* ptr = bracket + 1;
    for (int i = 0; i < SEMG_TOTAL_FEATURES; i++)
    {
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n' || *ptr == ',')
        {
            ptr++;
        }
        if (*ptr == '\0' || *ptr == ']')
        {
            return -3;
        }
        char* end_ptr = NULL;
        out_f[i]      = strtof(ptr, &end_ptr);
        if (end_ptr == ptr)
        {
            return -4;
        }
        ptr = end_ptr;
    }

    return 0;
}

static int post_process_vote(server_ctx_t* ctx, int* out_best_cls, float* out_conf)
{
    int best_cls    = 0;
    float best_prob = ctx->probs[0];

    for (int i = 1; i < SEMG_NUM_CLASSES; i++)
    {
        if (ctx->probs[i] > best_prob)
        {
            best_prob = ctx->probs[i];
            best_cls  = i;
        }
    }

    *out_best_cls = best_cls;
    *out_conf     = best_prob;

    if (best_prob < ctx->conf_threshold)
    {
        best_cls = 0;
    }

    int win = ctx->vote_window;
    if (win < 1)
        win = 1;
    if (win > MAX_VOTE_WINDOW)
        win = MAX_VOTE_WINDOW;

    ctx->vote_buf[ctx->vote_idx % win] = best_cls;
    ctx->vote_idx                      = (ctx->vote_idx + 1) % win;
    if (ctx->vote_count < win)
    {
        ctx->vote_count++;
        return -1;
    }

    for (int i = 0; i < win; i++)
    {
        if (ctx->vote_buf[i] != best_cls)
        {
            return -1;
        }
    }
    return best_cls;
}

static void publish_gesture_command(server_ctx_t* ctx, int seq, int gesture_id, float conf)
{
    if (ctx->mosq == NULL || gesture_id < 0 || gesture_id >= SEMG_NUM_CLASSES)
        return;

    char out_json[256];
    int len = snprintf(out_json,
                       sizeof(out_json),
                       "{\"seq\":%d,\"gesture_id\":%d,\"label\":\"%s\",\"action\":\"%s\",\"conf\":%.3f}",
                       seq,
                       gesture_id,
                       SEMG_GESTURE_LABELS[gesture_id],
                       ctx->gesture_actions[gesture_id],
                       conf);

    if (len > 0 && len < (int)sizeof(out_json))
    {
        if (mosquitto_publish(ctx->mosq, NULL, MQTT_TOPIC_PUB_CMD, len, out_json, 1, false) == MOSQ_ERR_SUCCESS)
        {
            ctx->total_tx_commands++;
            printf("  >>> [ACTION PUBLISHED] %s -> %s\n", MQTT_TOPIC_PUB_CMD, out_json);
            for (int i = 0; i < MAX_DEVICES; i++)
            {
                if (ctx->devices[i].active && ctx->devices[i].role == DEV_ROLE_END_DEVICE)
                {
                    snprintf(ctx->devices[i].last_status,
                             sizeof(ctx->devices[i].last_status),
                             "%s (%s)",
                             SEMG_GESTURE_LABELS[gesture_id],
                             ctx->gesture_actions[gesture_id]);
                }
            }
        }
    }
}

static void extract_topic_device_id(const char* topic, int token_index, char* out_id, size_t max_len)
{
    snprintf(out_id, max_len, "unknown");
    if (topic == NULL)
        return;

    const char* start = topic;
    int idx           = 0;
    while (*start != '\0')
    {
        const char* slash = strchr(start, '/');
        size_t len        = (slash != NULL) ? (size_t)(slash - start) : strlen(start);
        if (idx == token_index && len > 0)
        {
            if (len >= max_len)
                len = max_len - 1;
            memcpy(out_id, start, len);
            out_id[len] = '\0';
            return;
        }
        if (slash == NULL)
            break;
        start = slash + 1;
        idx++;
    }
}

static void process_incoming_features(server_ctx_t* ctx, const char* dev_id, const char* payload, int payload_len)
{
    int seq = 0;
    if (parse_feature_payload(payload, payload_len, &seq, ctx->raw_features) != 0)
    {
        fprintf(stderr, "[WARN] Invalid JSON feature payload from %s\n", dev_id);
        return;
    }

    ctx->total_rx_packets++;
    ctx->last_seq = seq;
    snprintf(ctx->last_sensor_id, sizeof(ctx->last_sensor_id), "%s", dev_id);

    semg_normalize_features(ctx->raw_features, ctx->norm_features);
    if (ml_runtime_infer(&ctx->tflite, ctx->norm_features, ctx->probs) != 0)
    {
        return;
    }

    int raw_cls    = 0;
    float conf     = 0.0f;
    int stable_cls = post_process_vote(ctx, &raw_cls, &conf);

    ctx->current_raw_gesture = raw_cls;
    ctx->current_conf        = conf;

    upsert_device(ctx, dev_id, DEV_ROLE_SENSOR, seq, SEMG_GESTURE_LABELS[raw_cls]);
    push_log(ctx, dev_id, seq, raw_cls, (stable_cls >= 0) ? stable_cls : ctx->last_published_gesture, conf);

    printf("[INFER] dev=%s seq=%-4d | raw=%-9s (%.1f%%) | probs=[%.2f, %.2f, %.2f, %.2f]\n",
           dev_id,
           seq,
           SEMG_GESTURE_LABELS[raw_cls],
           conf * 100.0f,
           ctx->probs[0],
           ctx->probs[1],
           ctx->probs[2],
           ctx->probs[3]);

    if (stable_cls >= 0 && stable_cls != ctx->last_published_gesture)
    {
        ctx->last_published_gesture = stable_cls;
        publish_gesture_command(ctx, seq, stable_cls, conf);
    }
}

static void on_mqtt_connect(struct mosquitto* mosq, void* obj, int rc)
{
    server_ctx_t* ctx = (server_ctx_t*)obj;
    if (rc == 0)
    {
        ctx->mqtt_connected = true;
        printf("[MQTT] Connected to broker. Subscribing to '%s' & '%s'\n", MQTT_TOPIC_SUB_FEAT, MQTT_TOPIC_SUB_ENDDEV);
        mosquitto_subscribe(mosq, NULL, MQTT_TOPIC_SUB_FEAT, 0);
        mosquitto_subscribe(mosq, NULL, MQTT_TOPIC_SUB_ENDDEV, 0);
    }
    else
    {
        ctx->mqtt_connected = false;
        fprintf(stderr, "[MQTT] Connect failed with code %d\n", rc);
    }
}

static void on_mqtt_disconnect(struct mosquitto* mosq, void* obj, int rc)
{
    (void)mosq;
    server_ctx_t* ctx   = (server_ctx_t*)obj;
    ctx->mqtt_connected = false;
    printf("[MQTT] Disconnected (rc=%d). Reconnecting...\n", rc);
}

static void on_mqtt_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg)
{
    (void)mosq;
    server_ctx_t* ctx = (server_ctx_t*)obj;
    if (ctx == NULL || msg == NULL || msg->payload == NULL)
        return;

    if (strstr(msg->topic, "semg/v1/end_device/") == msg->topic)
    {
        char dev_id[48];
        extract_topic_device_id(msg->topic, 3, dev_id, sizeof(dev_id));
        char status_txt[64];
        int copy_len =
            (msg->payloadlen < (int)sizeof(status_txt) - 1) ? msg->payloadlen : ((int)sizeof(status_txt) - 1);
        memcpy(status_txt, msg->payload, (size_t)copy_len);
        status_txt[copy_len] = '\0';
        upsert_device(ctx, dev_id, DEV_ROLE_END_DEVICE, 0, status_txt);
        return;
    }

    char sensor_id[48];
    extract_topic_device_id(msg->topic, 2, sensor_id, sizeof(sensor_id));
    process_incoming_features(ctx, sensor_id, (const char*)msg->payload, msg->payloadlen);
}

#ifdef _WIN32
static void
send_http_response(SOCKET client, int status_code, const char* content_type, const char* body, size_t body_len)
{
    const char* status_text = (status_code == 200) ? "OK" : "Not Found";
    char header[512];
    int hlen = snprintf(header,
                        sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                        "Access-Control-Allow-Headers: Content-Type\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n\r\n",
                        status_code,
                        status_text,
                        content_type,
                        body_len);
    send(client, header, hlen, 0);
    if (body != NULL && body_len > 0)
    {
        send(client, body, (int)body_len, 0);
    }
}

static void serve_static_file(SOCKET client, const char* filepath, const char* content_type)
{
    FILE* fp = fopen(filepath, "rb");
    if (fp == NULL)
    {
        const char* err = "{\"error\":\"File not found\"}";
        send_http_response(client, 404, "application/json", err, strlen(err));
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0 || sz > 262144)
    {
        fclose(fp);
        return;
    }
    static char file_buf[262144];
    size_t read_bytes = fread(file_buf, 1, (size_t)sz, fp);
    fclose(fp);
    send_http_response(client, 200, content_type, file_buf, read_bytes);
}

static void build_state_json(server_ctx_t* ctx, char* buf, size_t max_len)
{
    time_t now = time(NULL);
    int offset = 0;

    int sensor_online = 0, sensor_total = 0;
    int enddev_online = 0, enddev_total = 0;
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (!ctx->devices[i].active)
            continue;
        bool online = (difftime(now, ctx->devices[i].last_seen) <= DEVICE_TIMEOUT_SEC);
        if (ctx->devices[i].role == DEV_ROLE_SENSOR)
        {
            sensor_total++;
            if (online)
                sensor_online++;
        }
        else
        {
            enddev_total++;
            if (online)
                enddev_online++;
        }
    }

    offset +=
        snprintf(buf + offset,
                 max_len - (size_t)offset,
                 "{"
                 "\"mqtt_connected\":%s,"
                 "\"engine\":\"%s\","
                 "\"uptime_sec\":%ld,"
                 "\"total_rx\":%u,"
                 "\"total_tx\":%u,"
                 "\"sensor_online\":%d,\"sensor_total\":%d,"
                 "\"enddev_online\":%d,\"enddev_total\":%d,"
                 "\"config\":{\"conf_threshold\":%.2f,\"vote_window\":%d,"
                 "\"actions\":[\"%s\",\"%s\",\"%s\",\"%s\"]},"
                 "\"latest\":{\"sensor_id\":\"%s\",\"seq\":%d,"
                 "\"raw_gesture\":%d,\"raw_label\":\"%s\",\"conf\":%.4f,"
                 "\"stable_gesture\":%d,\"stable_label\":\"%s\","
                 "\"probs\":[%.4f,%.4f,%.4f,%.4f],"
                 "\"features\":[",
                 ctx->mqtt_connected ? "true" : "false",
                 ctx->tflite.use_dll ? "TFLite C API DLL" : "Built-in C TFLite Engine",
                 (long)difftime(now, ctx->start_time),
                 ctx->total_rx_packets,
                 ctx->total_tx_commands,
                 sensor_online,
                 sensor_total,
                 enddev_online,
                 enddev_total,
                 ctx->conf_threshold,
                 ctx->vote_window,
                 ctx->gesture_actions[0],
                 ctx->gesture_actions[1],
                 ctx->gesture_actions[2],
                 ctx->gesture_actions[3],
                 ctx->last_sensor_id[0] ? ctx->last_sensor_id : "none",
                 ctx->last_seq,
                 ctx->current_raw_gesture,
                 SEMG_GESTURE_LABELS[ctx->current_raw_gesture],
                 ctx->current_conf,
                 (ctx->last_published_gesture >= 0) ? ctx->last_published_gesture : 0,
                 (ctx->last_published_gesture >= 0) ? SEMG_GESTURE_LABELS[ctx->last_published_gesture] : "RESTING",
                 ctx->probs[0],
                 ctx->probs[1],
                 ctx->probs[2],
                 ctx->probs[3]);

    for (int i = 0; i < SEMG_TOTAL_FEATURES; i++)
    {
        offset += snprintf(buf + offset, max_len - (size_t)offset, "%s%.5f", (i > 0) ? "," : "", ctx->raw_features[i]);
    }

    offset += snprintf(buf + offset, max_len - (size_t)offset, "]},\"devices\":[");
    bool first_dev = true;
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (!ctx->devices[i].active)
            continue;
        long age    = (long)difftime(now, ctx->devices[i].last_seen);
        bool online = (age <= DEVICE_TIMEOUT_SEC);
        offset += snprintf(buf + offset,
                           max_len - (size_t)offset,
                           "%s{\"id\":\"%s\",\"role\":\"%s\",\"online\":%s,\"age_sec\":%ld,\"packets\":%u,\"seq\":%d,"
                           "\"status\":\"%s\"}",
                           first_dev ? "" : ",",
                           ctx->devices[i].id,
                           (ctx->devices[i].role == DEV_ROLE_SENSOR) ? "SENSOR_MCU" : "END_DEVICE",
                           online ? "true" : "false",
                           age,
                           ctx->devices[i].packet_count,
                           ctx->devices[i].last_seq,
                           ctx->devices[i].last_status);
        first_dev = false;
    }

    offset += snprintf(buf + offset, max_len - (size_t)offset, "],\"logs\":[");
    for (int i = 0; i < ctx->log_count; i++)
    {
        int idx         = (ctx->log_head - 1 - i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
        log_entry_t* lg = &ctx->logs[idx];
        int st_cls      = (lg->stable_gesture >= 0 && lg->stable_gesture < SEMG_NUM_CLASSES) ? lg->stable_gesture : 0;
        offset += snprintf(buf + offset,
                           max_len - (size_t)offset,
                           "%s{\"time\":\"%s\",\"device_id\":\"%s\",\"seq\":%d,\"raw_label\":\"%s\",\"stable_label\":"
                           "\"%s\",\"conf\":%.3f}",
                           (i > 0) ? "," : "",
                           lg->timestamp,
                           lg->device_id,
                           lg->seq,
                           SEMG_GESTURE_LABELS[lg->raw_gesture],
                           SEMG_GESTURE_LABELS[st_cls],
                           lg->confidence);
    }
    offset += snprintf(buf + offset, max_len - (size_t)offset, "]}");
}

static void parse_json_string_field(const char* body, const char* key, char* out_val, size_t max_len)
{
    const char* pos = strstr(body, key);
    if (pos == NULL)
        return;
    const char* colon = strchr(pos, ':');
    if (colon == NULL)
        return;
    const char* q1 = strchr(colon, '"');
    if (q1 == NULL)
        return;
    const char* q2 = strchr(q1 + 1, '"');
    if (q2 == NULL)
        return;
    size_t len = (size_t)(q2 - (q1 + 1));
    if (len >= max_len)
        len = max_len - 1;
    memcpy(out_val, q1 + 1, len);
    out_val[len] = '\0';
}

static void handle_http_client(server_ctx_t* ctx, SOCKET client)
{
    char req[4096];
    int received = recv(client, req, sizeof(req) - 1, 0);
    if (received <= 0)
    {
        closesocket(client);
        return;
    }
    req[received] = '\0';

    if (strncmp(req, "OPTIONS ", 8) == 0)
    {
        send_http_response(client, 200, "text/plain", "", 0);
        closesocket(client);
        return;
    }

    const char* body = strstr(req, "\r\n\r\n");
    if (body != NULL)
        body += 4;

    if (strncmp(req, "GET /api/state", 14) == 0)
    {
        static char json_buf[32768];
        build_state_json(ctx, json_buf, sizeof(json_buf));
        send_http_response(client, 200, "application/json", json_buf, strlen(json_buf));
    }
    else if (strncmp(req, "POST /api/config", 16) == 0 && body != NULL)
    {
        const char* th_pos = strstr(body, "\"conf_threshold\"");
        if (th_pos != NULL)
        {
            const char* c = strchr(th_pos, ':');
            if (c != NULL)
            {
                float val = strtof(c + 1, NULL);
                if (val >= 0.10f && val <= 0.99f)
                    ctx->conf_threshold = val;
            }
        }
        const char* vw_pos = strstr(body, "\"vote_window\"");
        if (vw_pos != NULL)
        {
            const char* c = strchr(vw_pos, ':');
            if (c != NULL)
            {
                int val = (int)strtol(c + 1, NULL, 10);
                if (val >= 1 && val <= MAX_VOTE_WINDOW)
                {
                    ctx->vote_window = val;
                    ctx->vote_count  = 0;
                    ctx->vote_idx    = 0;
                }
            }
        }
        parse_json_string_field(body, "\"action_0\"", ctx->gesture_actions[0], 48);
        parse_json_string_field(body, "\"action_1\"", ctx->gesture_actions[1], 48);
        parse_json_string_field(body, "\"action_2\"", ctx->gesture_actions[2], 48);
        parse_json_string_field(body, "\"action_3\"", ctx->gesture_actions[3], 48);

        const char* ok = "{\"status\":\"ok\"}";
        send_http_response(client, 200, "application/json", ok, strlen(ok));
    }
    else if (strncmp(req, "POST /api/simulate", 18) == 0 && body != NULL)
    {
        char dev_id[48] = "esp32_sim";
        parse_json_string_field(body, "\"device_id\"", dev_id, sizeof(dev_id));
        process_incoming_features(ctx, dev_id, body, (int)strlen(body));
        const char* ok = "{\"status\":\"simulated\"}";
        send_http_response(client, 200, "application/json", ok, strlen(ok));
    }
    else if (strncmp(req, "POST /api/end_device", 20) == 0 && body != NULL)
    {
        char dev_id[48]     = "laptop_01";
        char status_str[64] = "READY";
        parse_json_string_field(body, "\"id\"", dev_id, sizeof(dev_id));
        parse_json_string_field(body, "\"status\"", status_str, sizeof(status_str));
        upsert_device(ctx, dev_id, DEV_ROLE_END_DEVICE, 0, status_str);
        const char* ok = "{\"status\":\"registered\"}";
        send_http_response(client, 200, "application/json", ok, strlen(ok));
    }
    else if (strncmp(req, "GET /style.css", 14) == 0)
    {
        serve_static_file(client, "ui/style.css", "text/css; charset=utf-8");
    }
    else if (strncmp(req, "GET /app.js", 11) == 0)
    {
        serve_static_file(client, "ui/app.js", "application/javascript; charset=utf-8");
    }
    else
    {
        serve_static_file(client, "ui/index.html", "text/html; charset=utf-8");
    }

    closesocket(client);
}

static SOCKET init_http_server(int port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return INVALID_SOCKET;

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET)
        return INVALID_SOCKET;

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    u_long nonblock = 1;
    ioctlsocket(srv, FIONBIO, &nonblock);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((u_short)port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(srv);
        return INVALID_SOCKET;
    }
    if (listen(srv, 16) == SOCKET_ERROR)
    {
        closesocket(srv);
        return INVALID_SOCKET;
    }
    return srv;
}
#endif

int main(int argc, char* argv[])
{
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.start_time             = time(NULL);
    g_ctx.conf_threshold         = 0.75f;
    g_ctx.vote_window            = 3;
    g_ctx.last_published_gesture = -1;

    snprintf(g_ctx.gesture_actions[0], 48, "IDLE_HOLD");
    snprintf(g_ctx.gesture_actions[1], 48, "GRIP_CLOSE");
    snprintf(g_ctx.gesture_actions[2], 48, "HAND_RELEASE");
    snprintf(g_ctx.gesture_actions[3], 48, "CURSOR_CLICK");

    /* Pre-register default end devices so GUI shows end-device fleet immediately */
    upsert_device(&g_ctx, "end_mcu_arm_01", DEV_ROLE_END_DEVICE, 0, "STANDBY");
    upsert_device(&g_ctx, "laptop_client_01", DEV_ROLE_END_DEVICE, 0, "LISTENING");

    const char* model_path  = "../ML_Model/semg_mlp.tflite";
    const char* broker_host = MQTT_DEFAULT_HOST;
    int broker_port         = MQTT_DEFAULT_PORT;
    int http_port           = HTTP_DEFAULT_PORT;
    bool self_test          = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--self-test") == 0)
        {
            self_test = true;
        }
        else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)
        {
            broker_host = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            broker_port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--http-port") == 0 && i + 1 < argc)
        {
            http_port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
        {
            model_path = argv[++i];
        }
    }

    if (ml_runtime_init(&g_ctx.tflite, model_path) != 0)
    {
        fprintf(stderr, "[FATAL] Failed to initialize ML runtime\n");
        return 1;
    }

    if (self_test)
    {
        const char* sample_json =
            "{\"seq\":1,\"f\":[0.0005,0.0007,0.08,52,88, 0.0006,0.0008,0.09,50,82, 0.0005,0.0007,0.08,55,90]}";
        int seq = 0;
        if (parse_feature_payload(sample_json, (int)strlen(sample_json), &seq, g_ctx.raw_features) == 0)
        {
            semg_normalize_features(g_ctx.raw_features, g_ctx.norm_features);
            ml_runtime_infer(&g_ctx.tflite, g_ctx.norm_features, g_ctx.probs);
            printf("[SELF-TEST] seq=%d -> Probs: RESTING=%.4f, FIST=%.4f, OPEN_HAND=%.4f, POINTING=%.4f\n",
                   seq,
                   g_ctx.probs[0],
                   g_ctx.probs[1],
                   g_ctx.probs[2],
                   g_ctx.probs[3]);
        }
        ml_runtime_deinit(&g_ctx.tflite);
        return 0;
    }

#ifdef _WIN32
    SOCKET http_srv = init_http_server(http_port);
    if (http_srv != INVALID_SOCKET)
    {
        printf("[GUI] Web Dashboard active at: http://localhost:%d\n", http_port);
    }
    else
    {
        fprintf(stderr, "[WARN] Could not bind HTTP port %d\n", http_port);
    }
#endif

    mosquitto_lib_init();
    g_ctx.mosq = mosquitto_new("semg_c_inference_server", true, &g_ctx);
    if (g_ctx.mosq == NULL)
    {
        fprintf(stderr, "[FATAL] Failed to allocate mosquitto client\n");
        ml_runtime_deinit(&g_ctx.tflite);
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_connect_callback_set(g_ctx.mosq, on_mqtt_connect);
    mosquitto_disconnect_callback_set(g_ctx.mosq, on_mqtt_disconnect);
    mosquitto_message_callback_set(g_ctx.mosq, on_mqtt_message);
    mosquitto_reconnect_delay_set(g_ctx.mosq, 1, 10, true);

    printf("[SERVER] Connecting to MQTT Broker %s:%d...\n", broker_host, broker_port);
    int rc = mosquitto_connect(g_ctx.mosq, broker_host, broker_port, MQTT_KEEPALIVE_SEC);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        fprintf(stderr, "[WARN] Initial connect returned %d (%s). Retrying in loop...\n", rc, mosquitto_strerror(rc));
    }

    while (g_running)
    {
        rc = mosquitto_loop(g_ctx.mosq, 25, 1);
        if (g_running && rc != MOSQ_ERR_SUCCESS)
        {
            static time_t last_retry = 0;
            time_t now               = time(NULL);
            if (difftime(now, last_retry) >= 2.0)
            {
                last_retry = now;
                mosquitto_reconnect(g_ctx.mosq);
            }
        }

#ifdef _WIN32
        if (http_srv != INVALID_SOCKET)
        {
            for (int k = 0; k < 4; k++)
            {
                SOCKET client = accept(http_srv, NULL, NULL);
                if (client == INVALID_SOCKET)
                    break;
                u_long blocking = 0;
                ioctlsocket(client, FIONBIO, &blocking);
                handle_http_client(&g_ctx, client);
            }
        }
#endif
    }

    printf("\n[SERVER] Shutting down cleanly...\n");
#ifdef _WIN32
    if (http_srv != INVALID_SOCKET)
        closesocket(http_srv);
    WSACleanup();
#endif
    mosquitto_disconnect(g_ctx.mosq);
    mosquitto_destroy(g_ctx.mosq);
    mosquitto_lib_cleanup();
    ml_runtime_deinit(&g_ctx.tflite);
    return 0;
}

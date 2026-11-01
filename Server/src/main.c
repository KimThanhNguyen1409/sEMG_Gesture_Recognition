#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "model_params.h"
#include "semg_features.h"
#include "tflite_weights.h"
#include <mosquitto.h>

#define MQTT_DEFAULT_HOST "127.0.0.1"
#define MQTT_DEFAULT_PORT 1883
#define MQTT_KEEPALIVE_SEC 60
#define MQTT_TOPIC_SUB "semg/v1/+/features"
#define MQTT_TOPIC_PUB "semg/v1/control/gesture"

#define CONFIDENCE_THRESHOLD 0.75f
#define VOTE_WINDOW_SIZE 3

/* TFLite C API opaque types & function pointers for dynamic DLL loading on Windows */
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
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_CONNECTING,
    CONN_STATE_CONNECTED
} conn_state_t;

typedef struct
{
    tflite_runtime_t tflite;
    conn_state_t mqtt_state;
    float raw_features[SEMG_TOTAL_FEATURES];
    float norm_features[SEMG_TOTAL_FEATURES];
    float probs[SEMG_NUM_CLASSES];
    int vote_buf[VOTE_WINDOW_SIZE];
    int vote_idx;
    int vote_count;
    int last_published_gesture;
} server_ctx_t;

static volatile sig_atomic_t g_running = 1;
static server_ctx_t g_ctx;

static void handle_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Built-in C inference executing exact tensors from semg_mlp.tflite (Zero dynamic allocation) */
static void mlp_infer_builtin(const float in_norm[SEMG_TOTAL_FEATURES], float out_probs[SEMG_NUM_CLASSES])
{
    float h1[64];
    float h2[32];
    float logits[SEMG_NUM_CLASSES];

    /* Layer 1: Dense(64, ReLU), W1 shape [64, 15] */
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

    /* Layer 2: Dense(32, ReLU), W2 shape [32, 64] */
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

    /* Layer 3: Dense(4, Softmax), W3 shape [4, 32] */
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

/* Zero-malloc JSON parser for {"seq": 10, "f": [f0, f1, ..., f14]} */
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

    if (best_prob < CONFIDENCE_THRESHOLD)
    {
        best_cls = 0; /* Fallback to RESTING if uncertain */
    }

    ctx->vote_buf[ctx->vote_idx] = best_cls;
    ctx->vote_idx                = (ctx->vote_idx + 1) % VOTE_WINDOW_SIZE;
    if (ctx->vote_count < VOTE_WINDOW_SIZE)
    {
        ctx->vote_count++;
        return -1;
    }

    for (int i = 0; i < VOTE_WINDOW_SIZE; i++)
    {
        if (ctx->vote_buf[i] != best_cls)
        {
            return -1;
        }
    }
    return best_cls;
}

static void on_mqtt_connect(struct mosquitto* mosq, void* obj, int rc)
{
    server_ctx_t* ctx = (server_ctx_t*)obj;
    if (rc == 0)
    {
        ctx->mqtt_state = CONN_STATE_CONNECTED;
        printf("[MQTT] Connected to broker. Subscribing to '%s'...\n", MQTT_TOPIC_SUB);
        mosquitto_subscribe(mosq, NULL, MQTT_TOPIC_SUB, 0);
    }
    else
    {
        ctx->mqtt_state = CONN_STATE_DISCONNECTED;
        fprintf(stderr, "[MQTT] Connection failed with code %d\n", rc);
    }
}

static void on_mqtt_disconnect(struct mosquitto* mosq, void* obj, int rc)
{
    (void)mosq;
    server_ctx_t* ctx = (server_ctx_t*)obj;
    ctx->mqtt_state   = CONN_STATE_DISCONNECTED;
    printf("[MQTT] Disconnected (rc=%d). Waiting for auto-reconnect...\n", rc);
}

static void on_mqtt_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg)
{
    server_ctx_t* ctx = (server_ctx_t*)obj;
    if (ctx == NULL || msg == NULL || msg->payload == NULL)
        return;

    int seq = 0;
    if (parse_feature_payload((const char*)msg->payload, msg->payloadlen, &seq, ctx->raw_features) != 0)
    {
        fprintf(stderr, "[WARN] Invalid JSON feature payload on topic %s\n", msg->topic);
        return;
    }

    semg_normalize_features(ctx->raw_features, ctx->norm_features);

    if (ml_runtime_infer(&ctx->tflite, ctx->norm_features, ctx->probs) != 0)
    {
        fprintf(stderr, "[ERR] ML inference failed\n");
        return;
    }

    int raw_cls    = 0;
    float conf     = 0.0f;
    int stable_cls = post_process_vote(ctx, &raw_cls, &conf);

    printf("[INFER] seq=%-4d | raw=%-9s (%.1f%%) | probs=[%.2f, %.2f, %.2f, %.2f]\n",
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

        char out_json[160];
        int len = snprintf(out_json,
                           sizeof(out_json),
                           "{\"seq\":%d,\"gesture_id\":%d,\"label\":\"%s\",\"conf\":%.3f}",
                           seq,
                           stable_cls,
                           SEMG_GESTURE_LABELS[stable_cls],
                           conf);

        if (len > 0 && len < (int)sizeof(out_json))
        {
            int pub_rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC_PUB, len, out_json, 1, false);
            if (pub_rc == MOSQ_ERR_SUCCESS)
            {
                printf("  >>> [ACTION PUBLISHED] %s -> %s\n", MQTT_TOPIC_PUB, out_json);
            }
        }
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.last_published_gesture = -1;

    const char* model_path  = "../ML_Model/semg_mlp.tflite";
    const char* broker_host = MQTT_DEFAULT_HOST;
    int broker_port         = MQTT_DEFAULT_PORT;
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

    mosquitto_lib_init();
    struct mosquitto* mosq = mosquitto_new("semg_c_inference_server", true, &g_ctx);
    if (mosq == NULL)
    {
        fprintf(stderr, "[FATAL] Failed to allocate mosquitto client\n");
        ml_runtime_deinit(&g_ctx.tflite);
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_connect_callback_set(mosq, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mosq, on_mqtt_disconnect);
    mosquitto_message_callback_set(mosq, on_mqtt_message);
    mosquitto_reconnect_delay_set(mosq, 1, 10, true);

    printf("[SERVER] Connecting to MQTT Broker %s:%d...\n", broker_host, broker_port);
    g_ctx.mqtt_state = CONN_STATE_CONNECTING;
    int rc           = mosquitto_connect(mosq, broker_host, broker_port, MQTT_KEEPALIVE_SEC);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        fprintf(stderr,
                "[WARN] Initial connect returned %d (%s). Will retry automatically...\n",
                rc,
                mosquitto_strerror(rc));
    }

    while (g_running)
    {
        rc = mosquitto_loop(mosq, 100, 1);
        if (g_running && rc != MOSQ_ERR_SUCCESS)
        {
#ifdef _WIN32
            Sleep(1000);
#endif
            mosquitto_reconnect(mosq);
        }
    }

    printf("\n[SERVER] Shutting down cleanly...\n");
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    ml_runtime_deinit(&g_ctx.tflite);
    return 0;
}

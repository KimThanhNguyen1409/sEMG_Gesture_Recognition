/**
 * @file semg_wifi_udp.c
 * @brief Wi-Fi STA FSM (Exponential Backoff + Jitter), NVS Dynamic Provisioning & Batched UDP Streamer.
 *
 * USB Provisioning Protocol (921600 baud, newline terminated):
 *   RX: WIFI_SET|<ssid>|<pass>|<dest_ip>|<port>\n
 *   TX: WIFI_ACK|OK|<ssid>|<dest_ip>|<port>\r\n   (then esp_restart)
 *   RX: WIFI_GET\n
 *   TX: WIFI_CFG|<ssid>|<dest_ip>|<port>|<0|1>|<esp_ip>\r\n
 */

#include "semg_wifi_udp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "SEMG_WIFI_UDP";

#define SEMG_NVS_NAMESPACE "semg_wifi"

/* ========================================================================= */
/*                  RUNTIME CONFIG (LOADED FROM NVS FLASH)                   */
/* ========================================================================= */

static char     s_cfg_ssid[33]     = SEMG_WIFI_DEFAULT_SSID;
static char     s_cfg_pass[65]     = SEMG_WIFI_DEFAULT_PASS;
static char     s_cfg_dest_ip[32]  = SEMG_UDP_DEFAULT_DEST_IP;
static uint16_t s_cfg_dest_port    = SEMG_UDP_DEFAULT_DEST_PORT;
static char     s_esp_ip_str[20]   = "0.0.0.0";

/* ========================================================================= */
/*                     WIFI CONNECTION FINITE STATE MACHINE                  */
/* ========================================================================= */

typedef enum {
    WIFI_FSM_IDLE = 0,
    WIFI_FSM_CONNECTING,
    WIFI_FSM_CONNECTED,
    WIFI_FSM_BACKOFF,
} wifi_fsm_state_t;

static volatile wifi_fsm_state_t s_wifi_state      = WIFI_FSM_IDLE;
static uint32_t                  s_backoff_ms      = 1000u;
static esp_timer_handle_t        s_reconnect_timer = NULL;

/* Static UDP socket & destination */
static int                s_udp_sock = -1;
static struct sockaddr_in s_dest_addr;

/* Static zero-malloc UDP batch buffer (Core 1 only) */
static char     s_udp_buf[SEMG_UDP_TX_BUF_SIZE];
static size_t   s_udp_len        = 0;
static uint16_t s_udp_batch_cnt  = 0;
static uint8_t  s_udp_batch_mode = 0xFF;

/* Static zero-malloc UART RX line buffer (Core 1 only) */
static char     s_uart_rx_line[256];
static size_t   s_uart_rx_len = 0;

/* ========================================================================= */
/*                         NVS LOAD / SAVE HELPERS                           */
/* ========================================================================= */

static void nvs_load_wifi_config(void)
{
    nvs_handle_t hdl;
    esp_err_t err = nvs_open(SEMG_NVS_NAMESPACE, NVS_READONLY, &hdl);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved NVS Wi-Fi config -> using defaults (SSID='%s', Target=%s:%u)",
                 s_cfg_ssid, s_cfg_dest_ip, (unsigned int)s_cfg_dest_port);
        return;
    }

    size_t len = sizeof(s_cfg_ssid);
    if (nvs_get_str(hdl, "ssid", s_cfg_ssid, &len) != ESP_OK) {
        snprintf(s_cfg_ssid, sizeof(s_cfg_ssid), "%s", SEMG_WIFI_DEFAULT_SSID);
    }

    len = sizeof(s_cfg_pass);
    if (nvs_get_str(hdl, "pass", s_cfg_pass, &len) != ESP_OK) {
        snprintf(s_cfg_pass, sizeof(s_cfg_pass), "%s", SEMG_WIFI_DEFAULT_PASS);
    }

    len = sizeof(s_cfg_dest_ip);
    if (nvs_get_str(hdl, "dest_ip", s_cfg_dest_ip, &len) != ESP_OK) {
        snprintf(s_cfg_dest_ip, sizeof(s_cfg_dest_ip), "%s", SEMG_UDP_DEFAULT_DEST_IP);
    }

    uint16_t saved_port = 0;
    if (nvs_get_u16(hdl, "dest_port", &saved_port) == ESP_OK && saved_port > 0) {
        s_cfg_dest_port = saved_port;
    }

    nvs_close(hdl);
    ESP_LOGI(TAG, "Loaded NVS Wi-Fi config: SSID='%s', Target=%s:%u",
             s_cfg_ssid, s_cfg_dest_ip, (unsigned int)s_cfg_dest_port);
}

static esp_err_t nvs_save_wifi_config(const char *ssid, const char *pass,
                                      const char *dest_ip, uint16_t dest_port)
{
    if (ssid == NULL || pass == NULL || dest_ip == NULL || ssid[0] == '\0' || dest_ip[0] == '\0' || dest_port == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t hdl;
    esp_err_t err = nvs_open(SEMG_NVS_NAMESPACE, NVS_READWRITE, &hdl);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(hdl, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(hdl, "pass", pass);
    if (err == ESP_OK) err = nvs_set_str(hdl, "dest_ip", dest_ip);
    if (err == ESP_OK) err = nvs_set_u16(hdl, "dest_port", dest_port);
    if (err == ESP_OK) err = nvs_commit(hdl);

    nvs_close(hdl);
    return err;
}

/* ========================================================================= */
/*                          WIFI EVENT FSM HANDLERS                          */
/* ========================================================================= */

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (s_wifi_state == WIFI_FSM_BACKOFF) {
        s_wifi_state = WIFI_FSM_CONNECTING;
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect() failed (%s)", esp_err_to_name(err));
        }
    }
}

static const char *wifi_reason_str(uint8_t reason)
{
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:          return "NO_AP_FOUND (Sai ten SSID hoac khong phai 2.4G)";
        case WIFI_REASON_AUTH_FAIL:            return "AUTH_FAIL (Sai mat khau/bao mat)";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_TIMEOUT (Sai mat khau)";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "HANDSHAKE_TIMEOUT (Sai mat khau)";
        case WIFI_REASON_AUTH_EXPIRE:          return "AUTH_EXPIRE";
        case WIFI_REASON_ASSOC_FAIL:           return "ASSOC_FAIL";
        case WIFI_REASON_CONNECTION_FAIL:      return "CONNECTION_FAIL";
        default:                               return "OTHER";
    }
}

static void schedule_reconnect_with_backoff(uint8_t reason)
{
    if (s_reconnect_timer == NULL) {
        return;
    }

    snprintf(s_esp_ip_str, sizeof(s_esp_ip_str), "0.0.0.0");

    uint32_t jitter_ms = esp_random() % 500u;
    uint32_t delay_ms  = s_backoff_ms + jitter_ms;

    ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%u: %s). Reconnecting in %lu ms...",
             (unsigned int)reason, wifi_reason_str(reason), (unsigned long)delay_ms);
    s_wifi_state = WIFI_FSM_BACKOFF;

    esp_timer_stop(s_reconnect_timer);
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start reconnect timer (%s)", esp_err_to_name(err));
    }

    if (s_backoff_ms < 16000u) {
        s_backoff_ms <<= 1;
        if (s_backoff_ms > 16000u) {
            s_backoff_ms = 16000u;
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_state = WIFI_FSM_CONNECTING;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = (disc != NULL) ? disc->reason : 0;
        schedule_reconnect_with_backoff(reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_esp_ip_str, sizeof(s_esp_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        s_backoff_ms = 1000u;
        s_wifi_state = WIFI_FSM_CONNECTED;
        ESP_LOGI(TAG, "Wi-Fi Connected! ESP32 IP: %s -> UDP Target %s:%u",
                 s_esp_ip_str, s_cfg_dest_ip, (unsigned int)s_cfg_dest_port);
    }
}

/* ========================================================================= */
/*                           UART PROVISIONING                               */
/* ========================================================================= */

static wifi_ap_record_t s_scan_aps[15];

static void handle_uart_command(char *line)
{
    char reply[192];

    if (strcmp(line, "WIFI_SCAN") == 0) {
        if (s_reconnect_timer != NULL) {
            esp_timer_stop(s_reconnect_timer);
        }
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(50));
        wifi_scan_config_t scan_cfg = {
            .ssid        = NULL,
            .bssid       = NULL,
            .channel     = 0,
            .show_hidden = false,
        };
        if (esp_wifi_scan_start(&scan_cfg, true) == ESP_OK) {
            uint16_t ap_num = 15;
            if (esp_wifi_scan_get_ap_records(&ap_num, s_scan_aps) == ESP_OK) {
                for (uint16_t i = 0; i < ap_num; i++) {
                    int n = snprintf(reply, sizeof(reply), "\r\nWIFI_AP|%s|%d|%d\r\n",
                                     (const char *)s_scan_aps[i].ssid,
                                     (int)s_scan_aps[i].rssi,
                                     (int)s_scan_aps[i].authmode);
                    if (n > 0 && n < (int)sizeof(reply)) {
                        uart_write_bytes(SEMG_UART_PORT, reply, (size_t)n);
                    }
                }
            }
        }
        const char *done_msg = "\r\nWIFI_SCAN_DONE\r\n";
        uart_write_bytes(SEMG_UART_PORT, done_msg, strlen(done_msg));
        s_backoff_ms = 1000u;
        s_wifi_state = WIFI_FSM_CONNECTING;
        esp_wifi_connect();
        return;
    }

    if (strcmp(line, "WIFI_GET") == 0) {
        int n = snprintf(reply, sizeof(reply), "\r\nWIFI_CFG|%s|%s|%u|%d|%s\r\n",
                         s_cfg_ssid, s_cfg_dest_ip, (unsigned int)s_cfg_dest_port,
                         (s_wifi_state == WIFI_FSM_CONNECTED) ? 1 : 0, s_esp_ip_str);
        if (n > 0 && n < (int)sizeof(reply)) {
            uart_write_bytes(SEMG_UART_PORT, reply, (size_t)n);
        }
        return;
    }

    if (strncmp(line, "WIFI_SET|", 9) == 0) {
        char *p1 = line + 9;
        char *sep1 = strchr(p1, '|');
        if (sep1 == NULL) {
            goto bad_cmd;
        }
        *sep1 = '\0';

        char *p2 = sep1 + 1;
        char *sep2 = strchr(p2, '|');
        if (sep2 == NULL) {
            goto bad_cmd;
        }
        *sep2 = '\0';

        char *p3 = sep2 + 1;
        char *sep3 = strchr(p3, '|');
        if (sep3 == NULL) {
            goto bad_cmd;
        }
        *sep3 = '\0';

        char *p4 = sep3 + 1;
        long port_val = strtol(p4, NULL, 10);
        if (port_val < 1 || port_val > 65535) {
            goto bad_cmd;
        }

        esp_err_t err = nvs_save_wifi_config(p1, p2, p3, (uint16_t)port_val);
        if (err == ESP_OK) {
            int n = snprintf(reply, sizeof(reply), "\r\nWIFI_ACK|OK|%s|%s|%ld\r\n", p1, p3, port_val);
            if (n > 0 && n < (int)sizeof(reply)) {
                uart_write_bytes(SEMG_UART_PORT, reply, (size_t)n);
            }
            uart_wait_tx_idle_polling(SEMG_UART_PORT);
            vTaskDelay(pdMS_TO_TICKS(150));
            esp_restart();
        } else {
            int n = snprintf(reply, sizeof(reply), "\r\nWIFI_ACK|ERR|%s\r\n", esp_err_to_name(err));
            if (n > 0 && n < (int)sizeof(reply)) {
                uart_write_bytes(SEMG_UART_PORT, reply, (size_t)n);
            }
        }
        return;
    }

    return;

bad_cmd:
    {
        const char *err_msg = "\r\nWIFI_ACK|ERR|INVALID_FORMAT\r\n";
        uart_write_bytes(SEMG_UART_PORT, err_msg, strlen(err_msg));
    }
}

void semg_wifi_udp_poll_uart_cmd(void)
{
    uint8_t chunk[64];
    int n = uart_read_bytes(SEMG_UART_PORT, chunk, sizeof(chunk), 0);
    if (n <= 0) {
        return;
    }

    for (int i = 0; i < n; i++) {
        char c = (char)chunk[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (s_uart_rx_len > 0) {
                s_uart_rx_line[s_uart_rx_len] = '\0';
                handle_uart_command(s_uart_rx_line);
                s_uart_rx_len = 0;
            }
        } else if (s_uart_rx_len < (sizeof(s_uart_rx_line) - 1)) {
            s_uart_rx_line[s_uart_rx_len++] = c;
        } else {
            /* Line too long -> reset buffer safely */
            s_uart_rx_len = 0;
        }
    }
}

/* ========================================================================= */
/*                           PUBLIC INIT & UDP TX                            */
/* ========================================================================= */

esp_err_t semg_wifi_udp_init(void)
{
    /* 1. Initialize NVS & load saved Wi-Fi credentials */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed (%s)", esp_err_to_name(ret));
        return ret;
    }
    nvs_load_wifi_config();

    /* 2. Create one-shot timer for exponential backoff reconnect */
    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name     = "wifi_backoff",
    };
    ret = esp_timer_create(&timer_args, &s_reconnect_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* 3. Initialize lwIP netif & default event loop */
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    esp_netif_create_default_wifi_sta();

    /* 4. Configure Wi-Fi STA */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%.*s",
             (int)(sizeof(wifi_cfg.sta.ssid) - 1), s_cfg_ssid);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%.*s",
             (int)(sizeof(wifi_cfg.sta.password) - 1), s_cfg_pass);
    wifi_cfg.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;
    wifi_cfg.sta.sort_method        = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_cfg.sta.threshold.authmode = (s_cfg_pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Disable Wi-Fi modem power-save for low-jitter real-time UDP streaming */
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* 5. Create non-blocking UDP socket */
    s_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_udp_sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket (errno=%d)", errno);
        return ESP_FAIL;
    }

    int flags = fcntl(s_udp_sock, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(s_udp_sock, F_SETFL, flags | O_NONBLOCK);
    }

    memset(&s_dest_addr, 0, sizeof(s_dest_addr));
    s_dest_addr.sin_family      = AF_INET;
    s_dest_addr.sin_port        = htons(s_cfg_dest_port);
    s_dest_addr.sin_addr.s_addr = inet_addr(s_cfg_dest_ip);

    ESP_LOGI(TAG, "Wi-Fi STA connecting to SSID='%s' -> UDP %s:%u",
             s_cfg_ssid, s_cfg_dest_ip, (unsigned int)s_cfg_dest_port);
    return ESP_OK;
}

bool semg_wifi_udp_is_connected(void)
{
    return (s_wifi_state == WIFI_FSM_CONNECTED);
}

static void flush_udp_batch(void)
{
    if (s_udp_len == 0 || s_udp_sock < 0 || s_wifi_state != WIFI_FSM_CONNECTED) {
        s_udp_len       = 0;
        s_udp_batch_cnt = 0;
        return;
    }

    (void)sendto(s_udp_sock, s_udp_buf, s_udp_len, 0,
                 (const struct sockaddr *)&s_dest_addr, sizeof(s_dest_addr));

    s_udp_len       = 0;
    s_udp_batch_cnt = 0;
}

void semg_wifi_udp_push_frame(const semg_processed_frame_t *frame)
{
    if (frame == NULL || s_udp_sock < 0 || s_wifi_state != WIFI_FSM_CONNECTED) {
        return;
    }

    if (s_udp_batch_cnt > 0 && frame->output_rti_mode != s_udp_batch_mode) {
        flush_udp_batch();
    }

    if (s_udp_batch_cnt == 0) {
        s_udp_batch_mode = frame->output_rti_mode;
        int hdr = snprintf(s_udp_buf, sizeof(s_udp_buf), "#M%u\n", (unsigned int)s_udp_batch_mode);
        s_udp_len = (hdr > 0 && (size_t)hdr < sizeof(s_udp_buf)) ? (size_t)hdr : 0;
    }

    size_t remain = (s_udp_len < sizeof(s_udp_buf)) ? (sizeof(s_udp_buf) - s_udp_len) : 0;
    if (remain < 40) {
        flush_udp_batch();
        s_udp_batch_mode = frame->output_rti_mode;
        int hdr = snprintf(s_udp_buf, sizeof(s_udp_buf), "#M%u\n", (unsigned int)s_udp_batch_mode);
        s_udp_len = (hdr > 0 && (size_t)hdr < sizeof(s_udp_buf)) ? (size_t)hdr : 0;
        remain = sizeof(s_udp_buf) - s_udp_len;
    }

    int written = 0;
    if (frame->output_rti_mode) {
        written = snprintf(s_udp_buf + s_udp_len, remain, "%ld,%ld,%ld\n",
                           (long)frame->emg_rti_tenth_uv[0],
                           (long)frame->emg_rti_tenth_uv[1],
                           (long)frame->emg_rti_tenth_uv[2]);
    } else {
        written = snprintf(s_udp_buf + s_udp_len, remain, "%d,%d,%d\n",
                           (int)frame->voltage_mv[0],
                           (int)frame->voltage_mv[1],
                           (int)frame->voltage_mv[2]);
    }

    if (written > 0 && (size_t)written < remain) {
        s_udp_len += (size_t)written;
        s_udp_batch_cnt++;
    }

    if (s_udp_batch_cnt >= SEMG_UDP_BATCH_SAMPLES) {
        flush_udp_batch();
    }
}

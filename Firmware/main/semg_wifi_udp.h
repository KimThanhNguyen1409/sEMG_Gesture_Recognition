/**
 * @file semg_wifi_udp.h
 * @brief Wi-Fi STA (FSM + Exponential Backoff), NVS Dynamic Config, UART Provisioning
 *        and Zero-Malloc Batched UDP Streamer for sEMG.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "semg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NVS (loading saved SSID/PASS/IP/PORT or defaults), TCP/IP stack,
 *        Wi-Fi STA FSM and non-blocking UDP socket. Never blocks boot.
 */
esp_err_t semg_wifi_udp_init(void);

/**
 * @brief Queue one processed 1 kHz frame into the static UDP batch buffer.
 *        Flushes one UDP datagram every SEMG_UDP_BATCH_SAMPLES frames. Zero malloc.
 */
void semg_wifi_udp_push_frame(const semg_processed_frame_t *frame);

/**
 * @brief Non-blocking poll of UART0 for USB provisioning commands from PC Launcher App:
 *          - WIFI_SET|<ssid>|<pass>|<dest_ip>|<port>\n  -> saves to NVS & reboots
 *          - WIFI_GET\n                                 -> replies WIFI_CFG|...
 */
void semg_wifi_udp_poll_uart_cmd(void);

/**
 * @brief Check whether Wi-Fi STA currently has a valid IPv4 address.
 */
bool semg_wifi_udp_is_connected(void);

#ifdef __cplusplus
}
#endif

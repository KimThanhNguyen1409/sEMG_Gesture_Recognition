/**
 * @file semg_uart.h
 * @brief High-speed UART output driver for streaming processed sEMG frames.
 */

#pragma once

#include "esp_err.h"
#include "semg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UART interface for streaming.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t semg_uart_init(void);

/**
 * @brief Send a single processed multi-channel frame over UART in CSV format.
 *
 * @param[in] frame Pointer to processed frame.
 */
void semg_uart_send_frame(const semg_processed_frame_t *frame);

#ifdef __cplusplus
}
#endif

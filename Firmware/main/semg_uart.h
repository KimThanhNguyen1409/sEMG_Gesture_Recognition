/**
 * @file semg_uart.h
 * @brief High-speed UART output driver for serial plotter and terminal visualization.
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
 * @brief Send a single multi-channel sample over UART in CSV format ("ch1,ch2,ch3\r\n").
 *
 * @param[in] sample Pointer to sample structure.
 */
void semg_uart_send_sample(const semg_sample_t *sample);

#ifdef __cplusplus
}
#endif

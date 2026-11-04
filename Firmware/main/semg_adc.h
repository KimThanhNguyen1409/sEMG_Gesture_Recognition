/**
 * @file semg_adc.h
 * @brief Hardware ADC1 Oneshot and GPTimer driver for 1 kHz deterministic sampling.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "semg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ADC1 unit, configure channels (GPIO 34, 35, 32), and initialize eFuse calibration.
 *
 * @return esp_err_t ESP_OK on success, or appropriate error code.
 */
esp_err_t semg_adc_init(void);

/**
 * @brief Read all 3 configured sEMG channels.
 *
 * @param[out] out_mv  Array of calibrated voltages in millivolts [0..2]
 * @param[out] out_raw Array of raw 12-bit ADC counts [0..2]
 * @return esp_err_t   ESP_OK on success, or error code on read failure.
 */
esp_err_t semg_adc_read_all(int16_t out_mv[SEMG_NUM_CHANNELS], int16_t out_raw[SEMG_NUM_CHANNELS]);

/**
 * @brief Initialize the hardware GPTimer to trigger an alarm every 1000 us (1 kHz).
 *
 * @param[in] task_to_notify FreeRTOS TaskHandle to wake up on each timer tick.
 * @return esp_err_t         ESP_OK on success.
 */
esp_err_t semg_timer_init(TaskHandle_t task_to_notify);

/**
 * @brief Start the sampling GPTimer.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t semg_timer_start(void);

/**
 * @brief Stop the sampling GPTimer.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t semg_timer_stop(void);

#ifdef __cplusplus
}
#endif

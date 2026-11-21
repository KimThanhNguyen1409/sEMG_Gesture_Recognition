/**
 * @file semg_adc.h
 * @brief Continuous ADC DMA driver with Ping-Pong Buffering and Crosstalk Immunity.
 */

#pragma once

#include "esp_err.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "semg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Continuous ADC DMA with 3-channel pattern on ADC1,
 *        eFuse line-fitting calibration, and Ping-Pong double buffering.
 *
 * @param[in] worker_task_to_notify TaskHandle of the processing worker to unblock on DMA frame ready.
 * @return esp_err_t                ESP_OK on success.
 */
esp_err_t semg_adc_dma_init(TaskHandle_t worker_task_to_notify);

/**
 * @brief Start continuous DMA sampling hardware.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t semg_adc_dma_start(void);

/**
 * @brief Stop continuous DMA sampling hardware.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t semg_adc_dma_stop(void);

/**
 * @brief Read latest conversion frame from DMA into the Ping-Pong buffer.
 *
 * @param[out] out_dest Pointer to destination buffer (must be at least SEMG_DMA_FRAME_SIZE bytes).
 * @param[out] out_len  Number of bytes actually read.
 * @return esp_err_t    ESP_OK on success, or ESP_ERR_TIMEOUT if frame not ready.
 */
esp_err_t semg_adc_dma_read_frame(uint8_t *out_dest, uint32_t *out_len);

/**
 * @brief Map ADC hardware channel (4, 6, 7) to logical channel index (0, 1, 2).
 *
 * @param[in] hardware_chan ADC channel (e.g. ADC_CHANNEL_6).
 * @return int              Logical index 0..2, or -1 if invalid.
 */
int semg_adc_map_channel(uint8_t hardware_chan);

/**
 * @brief Get the eFuse calibration handle.
 *
 * @return adc_cali_handle_t Calibration handle or NULL if not supported.
 */
adc_cali_handle_t semg_adc_get_cali_handle(void);

#ifdef __cplusplus
}
#endif

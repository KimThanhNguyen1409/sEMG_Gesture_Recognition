/**
 * @file semg_pipeline.h
 * @brief Zero-floating-point preprocessing pipeline for sEMG signals:
 *        1. Validation (Range & Stuck-At Check)
 *        2. Oversampling Decimation & Averaging
 *        3. Moving Average Boxcar FIR Filtering
 *        4. eFuse Calibration (Integer mV)
 *        5. Linearization Curve Compensation
 *        6. Physical Unit Scaling (Referred-To-Input uV)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_adc/adc_cali.h"
#include "semg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Channel Range & Stuck-At Validator state.
 */
typedef struct {
    int16_t  last_raw;
    uint16_t stuck_counter;
    uint8_t  health;
} semg_validator_t;

/**
 * @brief Boxcar FIR Moving Average filter state (pure integer).
 */
typedef struct {
    int32_t buffer[SEMG_BOXCAR_TAPS];
    int32_t accumulator;
    uint8_t index;
} semg_boxcar_t;

/**
 * @brief Pipeline internal state.
 */
typedef struct {
    semg_validator_t  validators[SEMG_NUM_CHANNELS];
    semg_boxcar_t     boxcars[SEMG_NUM_CHANNELS];
    adc_cali_handle_t cali_handle;
} semg_pipeline_state_t;

/**
 * @brief Initialize all pipeline stages (Zero dynamic memory allocation).
 *
 * @param[out] pipe        Pointer to pipeline state structure.
 * @param[in]  cali_handle Initialized ADC eFuse calibration handle.
 */
void semg_pipeline_init(semg_pipeline_state_t *pipe, adc_cali_handle_t cali_handle);

/**
 * @brief Execute complete 6-stage integer preprocessing pipeline on a multi-channel batch.
 *
 * @param[in,out] pipe        Pipeline state.
 * @param[in]     raw_accum   Accumulated raw ADC readings per channel from oversampling window.
 * @param[in]     raw_count   Number of samples accumulated per channel.
 * @param[out]    out_frame   Output processed frame with health status, mV, and uV RTI.
 */
void semg_pipeline_process_frame(semg_pipeline_state_t *pipe,
                                 const int32_t raw_accum[SEMG_NUM_CHANNELS],
                                 const uint16_t raw_count[SEMG_NUM_CHANNELS],
                                 semg_processed_frame_t *out_frame);

/**
 * @brief Integer Piecewise Linearization curve compensation for ESP32 SAR ADC.
 *
 * @param[in] raw_mv Calibrated input voltage in millivolts.
 * @return int32_t   Linearized output voltage in millivolts.
 */
int32_t semg_linearize_mv(int32_t raw_mv);

/**
 * @brief Convert ADC voltage at pin to Referred-To-Input (RTI) physical sEMG in microvolts.
 *
 * @param[in] adc_mv Linearized ADC voltage in millivolts.
 * @return int32_t   RTI muscle potential in microvolts (uV).
 */
int32_t semg_scale_to_rti_uv(int32_t adc_mv);

#ifdef __cplusplus
}
#endif

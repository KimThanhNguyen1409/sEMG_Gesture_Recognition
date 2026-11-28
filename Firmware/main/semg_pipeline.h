/**
 * @file semg_pipeline.h
 * @brief v3 Zero-floating-point preprocessing pipeline for sEMG signals:
 *        1. Full-precision Oversampling Decimation (int64_t, no early truncation)
 *        2. Validation (Rail Saturation & Range+Mode Stuck-At Check)
 *        3. Sub-LSB eFuse Calibration Interpolation (in 0.01 mV)
 *        4. Piecewise Linearization Curve Compensation (clamped [0, 3300 mV])
 *        5. Auto-Tare DC Offset & Voltage Divider Compensation (SIMULATE mode)
 *        6. High-Resolution Physical Unit Scaling (0.1 uV Referred-To-Input)
 *        7. Optional Q14 Integer 50 Hz Biquad Notch (SEMG_ENABLE_NOTCH50)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "semg_config.h"

#ifndef HOST_SIM
#include "esp_adc/adc_cali.h"
#else
typedef void *adc_cali_handle_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Channel Range & Stuck-At Validator state (100-sample sliding window).
 */
typedef struct {
    int16_t window[SEMG_STUCK_WINDOW_LEN];
    uint8_t idx;
    uint8_t count;
    uint8_t health;
} semg_validator_t;

/**
 * @brief Fixed-point Q14 Direct Form II Transposed 50 Hz Biquad Notch state.
 */
typedef struct {
    int64_t d1;
    int64_t d2;
} semg_notch50_q14_t;

/**
 * @brief Pipeline internal state (static allocation friendly, zero malloc).
 */
typedef struct {
    semg_validator_t   validators[SEMG_NUM_CHANNELS];
    semg_notch50_q14_t notch50[SEMG_NUM_CHANNELS];
    int32_t            vref_mv_x100[SEMG_NUM_CHANNELS];
    int32_t            rolling_dc_x100[SEMG_NUM_CHANNELS];
    int64_t            tare_accum_x100[SEMG_NUM_CHANNELS];
    uint16_t           tare_count;
    bool               dc_seeded;
    adc_cali_handle_t  cali_handle;
} semg_pipeline_state_t;

/**
 * @brief Initialize all pipeline stages (Zero dynamic memory allocation).
 *
 * @param[out] pipe        Pointer to pipeline state structure.
 * @param[in]  cali_handle Initialized ADC eFuse calibration handle (or NULL).
 */
void semg_pipeline_init(semg_pipeline_state_t *pipe, adc_cali_handle_t cali_handle);

/**
 * @brief Lock current rolling resting DC voltage as the active Vref offset per channel.
 *        Used in SIMULATE mode when switching from CHECK_RAW_MV to AI_LIVE_V3.
 *
 * @param[in,out] pipe Pipeline state.
 */
void semg_pipeline_tare_now(semg_pipeline_state_t *pipe);

/**
 * @brief Execute v3 integer preprocessing pipeline on a multi-channel oversampled batch.
 *
 * @param[in,out] pipe        Pipeline state.
 * @param[in]     raw_accum   Accumulated raw ADC readings per channel from 20x oversampling window.
 * @param[in]     raw_count   Number of samples accumulated per channel.
 * @param[out]    out_frame   Output processed frame with health status, mV, and 0.1 uV RTI.
 */
void semg_pipeline_process_frame(semg_pipeline_state_t *pipe,
                                 const int32_t raw_accum[SEMG_NUM_CHANNELS],
                                 const uint16_t raw_count[SEMG_NUM_CHANNELS],
                                 semg_processed_frame_t *out_frame);

/**
 * @brief Integer Piecewise Linearization curve compensation in 0.01 mV units (clamped [0, 330000]).
 *
 * @param[in] raw_mv_x100 Calibrated input voltage in 0.01 mV units.
 * @return int32_t        Linearized & clamped output voltage in 0.01 mV units.
 */
int32_t semg_linearize_mv_x100(int32_t raw_mv_x100);

/**
 * @brief Convert ADC voltage (in 0.01 mV) to Referred-To-Input physical sEMG in 0.1 uV units
 *        using nominal SEMG_VREF_MV_X100.
 *
 * @param[in] adc_mv_x100 Linearized ADC voltage in 0.01 mV units.
 * @return int32_t        RTI muscle potential in 0.1 uV units (10 = 1.0 uV).
 */
int32_t semg_scale_to_rti_tenth_uv(int32_t adc_mv_x100);

#ifdef __cplusplus
}
#endif

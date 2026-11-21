/**
 * @file semg_pipeline.c
 * @brief Implementation of 6-stage integer preprocessing pipeline for sEMG signals.
 *
 * Rules:
 * - ZERO floating-point operations (100% integer math: int16_t, int32_t, bit-shifts).
 * - Safe against ISR / cache-coherency pitfalls.
 */

#include "semg_pipeline.h"
#include <string.h>

/* ========================================================================= */
/*                   PIECEWISE LINEAR COMPENSATION TABLE                     */
/* ========================================================================= */

typedef struct {
    int16_t x_mv;       /* Input measured voltage in mV */
    int16_t delta_mv;   /* Compensation offset to add in mV */
} semg_cali_knee_t;

/**
 * @brief Empirically characterized non-linearity compensation points for ESP32-D0WD SAR ADC (12dB Attenuation).
 * Smoothly lifts bottom offset and suppresses top compression without float math.
 */
static const semg_cali_knee_t KNEE_TABLE[] = {
    {    0,   70 },
    {  100,   45 },
    {  250,   20 },
    {  500,    0 },
    { 2000,    0 },
    { 2700,  -15 },
    { 3000,  -40 },
    { 3300,  -75 },
};
#define KNEE_POINTS_NUM (sizeof(KNEE_TABLE) / sizeof(KNEE_TABLE[0]))

int32_t semg_linearize_mv(int32_t raw_mv)
{
    if (raw_mv <= KNEE_TABLE[0].x_mv) {
        return raw_mv + KNEE_TABLE[0].delta_mv;
    }
    if (raw_mv >= KNEE_TABLE[KNEE_POINTS_NUM - 1].x_mv) {
        return raw_mv + KNEE_TABLE[KNEE_POINTS_NUM - 1].delta_mv;
    }

    /* Find segment for linear interpolation */
    for (size_t i = 0; i < KNEE_POINTS_NUM - 1; i++) {
        if (raw_mv >= KNEE_TABLE[i].x_mv && raw_mv < KNEE_TABLE[i + 1].x_mv) {
            int32_t x0 = KNEE_TABLE[i].x_mv;
            int32_t x1 = KNEE_TABLE[i + 1].x_mv;
            int32_t y0 = KNEE_TABLE[i].delta_mv;
            int32_t y1 = KNEE_TABLE[i + 1].delta_mv;

            /* Integer interpolation: delta = y0 + ((raw - x0) * (y1 - y0)) / (x1 - x0) */
            int32_t delta = y0 + ((raw_mv - x0) * (y1 - y0)) / (x1 - x0);
            int32_t corrected = raw_mv + delta;

            if (corrected < 0) {
                corrected = 0;
            } else if (corrected > 3300) {
                corrected = 3300;
            }
            return corrected;
        }
    }

    return raw_mv;
}

int32_t semg_scale_to_rti_uv(int32_t adc_mv)
{
    /* V_RTI (uV) = (V_ADC - V_REF) * 1000 / GAIN */
    int32_t diff_mv = adc_mv - SEMG_VREF_MV;
    return (diff_mv * 1000) / SEMG_AFE_GAIN;
}

void semg_pipeline_init(semg_pipeline_state_t *pipe, adc_cali_handle_t cali_handle)
{
    if (pipe == NULL) {
        return;
    }

    memset(pipe, 0, sizeof(semg_pipeline_state_t));
    pipe->cali_handle = cali_handle;

    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
        pipe->validators[ch].last_raw = -1;
        pipe->validators[ch].stuck_counter = 0;
        pipe->validators[ch].health = SEMG_HEALTH_OK;

        pipe->boxcars[ch].accumulator = 0;
        pipe->boxcars[ch].index = 0;
        memset(pipe->boxcars[ch].buffer, 0, sizeof(pipe->boxcars[ch].buffer));
    }
}

void semg_pipeline_process_frame(semg_pipeline_state_t *pipe,
                                 const int32_t raw_accum[SEMG_NUM_CHANNELS],
                                 const uint16_t raw_count[SEMG_NUM_CHANNELS],
                                 semg_processed_frame_t *out_frame)
{
    if (pipe == NULL || raw_accum == NULL || raw_count == NULL || out_frame == NULL) {
        return;
    }

    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
        /* ------------------------------------------------------------- */
        /* STAGE 1: Oversampling Decimation & Averaging                  */
        /* ------------------------------------------------------------- */
        int16_t avg_raw = 0;
        if (raw_count[ch] > 0) {
            avg_raw = (int16_t)(raw_accum[ch] / raw_count[ch]);
        }
        out_frame->raw_decimated[ch] = avg_raw;

        /* ------------------------------------------------------------- */
        /* STAGE 2: Range & Stuck-At Validation Check                    */
        /* ------------------------------------------------------------- */
        uint8_t flags = SEMG_HEALTH_OK;

        /* Range Check: Detect rail clipping */
        if (avg_raw <= SEMG_RAIL_MIN_RAW) {
            flags |= SEMG_HEALTH_RAIL_LOW;
        } else if (avg_raw >= SEMG_RAIL_MAX_RAW) {
            flags |= SEMG_HEALTH_RAIL_HIGH;
        }

        /* Stuck-At Check: Flatline electrode detection */
        int16_t diff = avg_raw - pipe->validators[ch].last_raw;
        if (diff < 0) {
            diff = -diff;
        }

        if (diff <= SEMG_STUCK_AT_DIFF_THRESH && pipe->validators[ch].last_raw >= 0) {
            if (pipe->validators[ch].stuck_counter < SEMG_STUCK_AT_MAX_SAMPLES) {
                pipe->validators[ch].stuck_counter++;
            }
            if (pipe->validators[ch].stuck_counter >= SEMG_STUCK_AT_MAX_SAMPLES) {
                flags |= SEMG_HEALTH_STUCK_AT;
            }
        } else {
            pipe->validators[ch].stuck_counter = 0;
        }

        pipe->validators[ch].last_raw = avg_raw;
        pipe->validators[ch].health = flags;
        out_frame->health_flags[ch] = flags;

        /* ------------------------------------------------------------- */
        /* STAGE 3: Boxcar FIR Moving Average Filter (Power-of-2 shift)  */
        /* ------------------------------------------------------------- */
        semg_boxcar_t *bc = &pipe->boxcars[ch];
        bc->accumulator -= bc->buffer[bc->index];
        bc->accumulator += avg_raw;
        bc->buffer[bc->index] = avg_raw;
        bc->index = (bc->index + 1) & (SEMG_BOXCAR_TAPS - 1);
        int32_t fir_raw = bc->accumulator >> SEMG_BOXCAR_SHIFT;

        /* ------------------------------------------------------------- */
        /* STAGE 4: eFuse Calibration (Integer Millivolts)              */
        /* ------------------------------------------------------------- */
        int calibrated_mv = 0;
        if (pipe->cali_handle != NULL) {
            esp_err_t err = adc_cali_raw_to_voltage(pipe->cali_handle, fir_raw, &calibrated_mv);
            if (err != ESP_OK) {
                calibrated_mv = (fir_raw * 3300) / 4095;
            }
        } else {
            calibrated_mv = (fir_raw * 3300) / 4095;
        }

        /* ------------------------------------------------------------- */
        /* STAGE 5: Linearization Curve Compensation                     */
        /* ------------------------------------------------------------- */
        int32_t linear_mv = semg_linearize_mv(calibrated_mv);
        out_frame->voltage_mv[ch] = (int16_t)linear_mv;

        /* ------------------------------------------------------------- */
        /* STAGE 6: Scaling: Physical Unit Conversion (RTI in uV)        */
        /* ------------------------------------------------------------- */
        out_frame->emg_rti_uv[ch] = semg_scale_to_rti_uv(linear_mv);
    }
}

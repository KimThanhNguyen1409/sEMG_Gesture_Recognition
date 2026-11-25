/**
 * @file semg_pipeline.c
 * @brief Implementation of v3 zero-floating-point preprocessing pipeline for sEMG signals.
 *
 * Key improvements over v1:
 * - Removed 8-tap Boxcar FIR @ 1 kHz (eliminated 125/250 Hz nulls & passband droop).
 * - Preserved 20x oversampling fractional bits using int64_t math and 0.1 uV RTI output.
 * - Fixed LUT >= 3300 mV boundary clamping to [0, 330000] (0.01 mV units).
 * - Fixed STUCK_AT false positives on RESTING via range <= 2 AND mode >= 90/100 check.
 * - Added optional Q14 integer 50 Hz Biquad Notch (SEMG_ENABLE_NOTCH50).
 */

#include "semg_pipeline.h"
#include <string.h>

/* ========================================================================= */
/*                   PIECEWISE LINEAR COMPENSATION TABLE                     */
/* ========================================================================= */

typedef struct {
    int32_t x_mv_x100;      /* Input measured voltage in 0.01 mV */
    int32_t delta_mv_x100;  /* Compensation offset to add in 0.01 mV */
} semg_cali_knee_t;

/**
 * @brief Non-linearity compensation points for ESP32-D0WD SAR ADC (12 dB Attenuation),
 * scaled by 100 (0.01 mV units) to preserve sub-LSB oversampling resolution.
 */
static const semg_cali_knee_t KNEE_TABLE[] = {
    {      0,   7000 },
    {  10000,   4500 },
    {  25000,   2000 },
    {  50000,      0 },
    { 200000,      0 },
    { 270000,  -1500 },
    { 300000,  -4000 },
    { 330000,  -7500 },
};
#define KNEE_POINTS_NUM (sizeof(KNEE_TABLE) / sizeof(KNEE_TABLE[0]))
#define MV_X100_MIN     (0)
#define MV_X100_MAX     (330000)

static inline int32_t clamp_mv_x100(int64_t val)
{
    if (val < MV_X100_MIN) {
        return MV_X100_MIN;
    }
    if (val > MV_X100_MAX) {
        return MV_X100_MAX;
    }
    return (int32_t)val;
}

int32_t semg_linearize_mv_x100(int32_t raw_mv_x100)
{
    if (raw_mv_x100 <= KNEE_TABLE[0].x_mv_x100) {
        return clamp_mv_x100((int64_t)raw_mv_x100 + KNEE_TABLE[0].delta_mv_x100);
    }
    if (raw_mv_x100 >= KNEE_TABLE[KNEE_POINTS_NUM - 1].x_mv_x100) {
        return clamp_mv_x100((int64_t)raw_mv_x100 + KNEE_TABLE[KNEE_POINTS_NUM - 1].delta_mv_x100);
    }

    for (size_t i = 0; i < KNEE_POINTS_NUM - 1; i++) {
        if (raw_mv_x100 >= KNEE_TABLE[i].x_mv_x100 && raw_mv_x100 < KNEE_TABLE[i + 1].x_mv_x100) {
            int64_t x0 = KNEE_TABLE[i].x_mv_x100;
            int64_t x1 = KNEE_TABLE[i + 1].x_mv_x100;
            int64_t y0 = KNEE_TABLE[i].delta_mv_x100;
            int64_t y1 = KNEE_TABLE[i + 1].delta_mv_x100;

            int64_t delta = y0 + (((int64_t)raw_mv_x100 - x0) * (y1 - y0)) / (x1 - x0);
            return clamp_mv_x100((int64_t)raw_mv_x100 + delta);
        }
    }

    return clamp_mv_x100(raw_mv_x100);
}

int32_t semg_scale_to_rti_tenth_uv(int32_t adc_mv_x100)
{
    /*
     * 1 unit of adc_mv_x100 = 0.01 mV = 10 uV = 100 * (0.1 uV).
     * Gain = SEMG_AFE_GAIN_X10 / 10 (e.g. 1136 / 10 = 113.6).
     * V_RTI (0.1 uV) = (diff_mv_x100 * 100 * 10) / SEMG_AFE_GAIN_X10
     */
    int64_t diff = (int64_t)adc_mv_x100 - SEMG_VREF_MV_X100;
    int64_t num = diff * 1000LL;
    /* Round-to-nearest integer division */
    if (num >= 0) {
        return (int32_t)((num + (SEMG_AFE_GAIN_X10 / 2)) / SEMG_AFE_GAIN_X10);
    } else {
        return (int32_t)((num - (SEMG_AFE_GAIN_X10 / 2)) / SEMG_AFE_GAIN_X10);
    }
}

/* ========================================================================= */
/*                  OPTIONAL Q14 INTEGER 50 HZ BIQUAD NOTCH                  */
/* ========================================================================= */

#if (SEMG_ENABLE_NOTCH50 == 1)
/* Q14 coefficients for fs = 1000 Hz, f0 = 50 Hz, Q = 5.0 (SCALE = 16384) */
#define NOTCH_Q14_SHIFT  (14)
#define NOTCH_B0_Q14     (15893LL)
#define NOTCH_B1_Q14     (-30230LL)
#define NOTCH_B2_Q14     (15893LL)
#define NOTCH_A1_Q14     (-30230LL)
#define NOTCH_A2_Q14     (15402LL)

static int32_t semg_notch50_process(semg_notch50_q14_t *st, int32_t input)
{
    int64_t x = (int64_t)input;
    int64_t y_q14 = (NOTCH_B0_Q14 * x) + st->d1;
    int64_t y = (y_q14 >= 0) ? ((y_q14 + (1LL << (NOTCH_Q14_SHIFT - 1))) >> NOTCH_Q14_SHIFT)
                             : -(((-y_q14) + (1LL << (NOTCH_Q14_SHIFT - 1))) >> NOTCH_Q14_SHIFT);

    st->d1 = (NOTCH_B1_Q14 * x) - (NOTCH_A1_Q14 * y) + st->d2;
    st->d2 = (NOTCH_B2_Q14 * x) - (NOTCH_A2_Q14 * y);
    return (int32_t)y;
}
#endif

/* ========================================================================= */
/*                      RANGE & STUCK-AT VALIDATION                          */
/* ========================================================================= */

static uint8_t semg_validate_channel(semg_validator_t *v, int16_t raw_code)
{
    uint8_t flags = SEMG_HEALTH_OK;

    /* 1. Rail Saturation Check */
    if (raw_code <= SEMG_RAIL_MIN_RAW) {
        flags |= SEMG_HEALTH_RAIL_LOW;
    } else if (raw_code >= SEMG_RAIL_MAX_RAW) {
        flags |= SEMG_HEALTH_RAIL_HIGH;
    }

    /* 2. Push sample into 100-sample circular window */
    v->window[v->idx] = raw_code;
    v->idx++;
    if (v->idx >= SEMG_STUCK_WINDOW_LEN) {
        v->idx = 0;
    }
    if (v->count < SEMG_STUCK_WINDOW_LEN) {
        v->count++;
    }

    /* 3. Stuck-At Check: range <= 2 AND >= 90/100 samples share the exact same code */
    if (v->count >= SEMG_STUCK_WINDOW_LEN) {
        int16_t min_v = v->window[0];
        int16_t max_v = v->window[0];
        for (int i = 1; i < SEMG_STUCK_WINDOW_LEN; i++) {
            int16_t s = v->window[i];
            if (s < min_v) min_v = s;
            if (s > max_v) max_v = s;
        }

        if ((max_v - min_v) <= SEMG_STUCK_MAX_RANGE) {
            /* At most 3 distinct codes exist: min_v, min_v + 1, min_v + 2 */
            uint8_t counts[3] = {0, 0, 0};
            for (int i = 0; i < SEMG_STUCK_WINDOW_LEN; i++) {
                int16_t offset = v->window[i] - min_v;
                if (offset >= 0 && offset <= 2) {
                    counts[offset]++;
                }
            }
            uint8_t mode_count = counts[0];
            if (counts[1] > mode_count) mode_count = counts[1];
            if (counts[2] > mode_count) mode_count = counts[2];

            if (mode_count >= SEMG_STUCK_MIN_SAME_COUNT) {
                flags |= SEMG_HEALTH_STUCK_AT;
            }
        }
    }

    v->health = flags;
    return flags;
}

/* ========================================================================= */
/*                           PIPELINE API                                    */
/* ========================================================================= */

void semg_pipeline_init(semg_pipeline_state_t *pipe, adc_cali_handle_t cali_handle)
{
    if (pipe == NULL) {
        return;
    }

    memset(pipe, 0, sizeof(semg_pipeline_state_t));
    pipe->cali_handle = cali_handle;
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
        uint16_t cnt = raw_count[ch];
        int32_t accum = raw_accum[ch];

        if (cnt == 0) {
            out_frame->raw_decimated[ch] = 0;
            out_frame->voltage_mv[ch] = 0;
            out_frame->emg_rti_tenth_uv[ch] = 0;
            out_frame->health_flags[ch] = SEMG_HEALTH_RAIL_LOW;
            continue;
        }

        /* ------------------------------------------------------------- */
        /* STAGE 1: Rounded raw code for diagnostics & validation        */
        /* ------------------------------------------------------------- */
        int16_t avg_raw = (int16_t)((accum + (int32_t)(cnt / 2)) / (int32_t)cnt);
        out_frame->raw_decimated[ch] = avg_raw;

        /* ------------------------------------------------------------- */
        /* STAGE 2: Range & Stuck-At Validation                          */
        /* ------------------------------------------------------------- */
        out_frame->health_flags[ch] = semg_validate_channel(&pipe->validators[ch], avg_raw);

        /* ------------------------------------------------------------- */
        /* STAGE 3: Full-Precision Calibration (in 0.01 mV units)        */
        /* ------------------------------------------------------------- */
        int32_t calibrated_mv_x100 = 0;

#ifndef HOST_SIM
        if (pipe->cali_handle != NULL) {
            int q = accum / (int)cnt;
            int r = accum % (int)cnt;
            int q_next = (q < 4095) ? (q + 1) : 4095;
            int mv0 = 0;
            int mv1 = 0;

            if (adc_cali_raw_to_voltage(pipe->cali_handle, q, &mv0) == ESP_OK &&
                adc_cali_raw_to_voltage(pipe->cali_handle, q_next, &mv1) == ESP_OK) {
                /* Sub-LSB linear interpolation preserves 20x oversampling bits */
                int64_t interp = ((int64_t)mv0 * 100LL) +
                                 (((int64_t)r * (int64_t)(mv1 - mv0) * 100LL) / (int64_t)cnt);
                calibrated_mv_x100 = clamp_mv_x100(interp);
            } else {
                calibrated_mv_x100 = clamp_mv_x100(((int64_t)accum * 330000LL) / ((int64_t)cnt * 4095LL));
            }
        } else {
            calibrated_mv_x100 = clamp_mv_x100(((int64_t)accum * 330000LL) / ((int64_t)cnt * 4095LL));
        }
#else
        /* Host simulation: exact int64_t ratio preserving full oversampling precision */
        calibrated_mv_x100 = clamp_mv_x100(((int64_t)accum * 330000LL) / ((int64_t)cnt * 4095LL));
#endif

        /* ------------------------------------------------------------- */
        /* STAGE 4: Piecewise Linearization Curve Compensation           */
        /* ------------------------------------------------------------- */
        int32_t linear_mv_x100 = semg_linearize_mv_x100(calibrated_mv_x100);
        out_frame->voltage_mv[ch] = (int16_t)((linear_mv_x100 + 50) / 100);

        /* ------------------------------------------------------------- */
        /* STAGE 5: High-Resolution Scaling to Physical Units (0.1 uV)   */
        /* ------------------------------------------------------------- */
        int32_t rti_tenth_uv = semg_scale_to_rti_tenth_uv(linear_mv_x100);

        /* ------------------------------------------------------------- */
        /* STAGE 6: Optional Q14 Integer 50 Hz Notch                     */
        /* ------------------------------------------------------------- */
#if (SEMG_ENABLE_NOTCH50 == 1)
        rti_tenth_uv = semg_notch50_process(&pipe->notch50[ch], rti_tenth_uv);
#endif
        out_frame->emg_rti_tenth_uv[ch] = rti_tenth_uv;
    }
}

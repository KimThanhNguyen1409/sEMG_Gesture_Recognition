/**
 * @file test_host_pipeline.c
 * @brief Native Windows MinGW-w64 unit tests for Firmware v3 semg_pipeline.c (-DHOST_SIM).
 *
 * Build & run:
 *   gcc -DHOST_SIM -IFirmware/main Firmware/main/semg_pipeline.c Firmware/tools/test_host_pipeline.c -lm -Wall -Wextra -o test_pipeline.exe
 *   ./test_pipeline.exe
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semg_pipeline.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void test_lut_clamping(void)
{
    /* Bug 6 regression: raw_mv >= 3300 must clamp to [0, 330000] */
    assert(semg_linearize_mv_x100(340000) <= 330000);
    assert(semg_linearize_mv_x100(400000) == 330000);
    assert(semg_linearize_mv_x100(-10000) == 0);
    /* Mid-range (500..2000 mV) is 1:1 linear */
    assert(semg_linearize_mv_x100(165000) == 165000);
    printf("[PASS] test_lut_clamping\n");
}

static void test_resolution_and_scaling(void)
{
    /* 1763.60 mV -> diff = +113.60 mV -> RTI = +1000.0 uV = +10000 (0.1 uV) */
    int32_t rti = semg_scale_to_rti_tenth_uv(176360);
    assert(rti == 10000);

    /* Sub-LSB oversampling: 2048*20 + 10 = 40970 (2048.5 codes) vs 2048*20 = 40960 */
    semg_pipeline_state_t pipe;
    semg_pipeline_init(&pipe, NULL);

    semg_processed_frame_t f0, f1;
    int32_t acc0[3] = {40960, 40960, 40960};
    int32_t acc1[3] = {40970, 40970, 40970};
    uint16_t cnt[3] = {20, 20, 20};

    semg_pipeline_process_frame(&pipe, acc0, cnt, &f0);
    semg_pipeline_process_frame(&pipe, acc1, cnt, &f1);

    /* 0.5 LSB @ 3300 mV / 4095 / 113.6 = ~3.55 uV = ~35 (0.1 uV) */
    int32_t delta = f1.emg_rti_tenth_uv[0] - f0.emg_rti_tenth_uv[0];
    assert(delta >= 30 && delta <= 40);
    printf("[PASS] test_resolution_and_scaling (0.5 LSB delta = %ld x 0.1 uV)\n", (long)delta);
}

static void test_stuck_at_no_false_positive_on_resting(void)
{
    semg_pipeline_state_t pipe;
    semg_pipeline_init(&pipe, NULL);

    semg_processed_frame_t frame;
    uint16_t cnt[3] = {20, 20, 20};
    int false_positives = 0;

    /* Simulate 500 ms of RESTING signal oscillating within +/- 1 LSB (range = 2) */
    for (int i = 0; i < 500; i++) {
        int16_t code = (int16_t)(2048 + (i % 3) - 1); /* 2047, 2048, 2049 -> ~33% each */
        int32_t acc[3] = {code * 20, code * 20, code * 20};
        semg_pipeline_process_frame(&pipe, acc, cnt, &frame);
        if (frame.health_flags[0] & SEMG_HEALTH_STUCK_AT) {
            false_positives++;
        }
    }
    assert(false_positives == 0);

    /* Now inject true flatline (constant 2048) -> must detect within 100 ms */
    int detected_at = -1;
    for (int i = 0; i < 120; i++) {
        int32_t acc[3] = {2048 * 20, 2048 * 20, 2048 * 20};
        semg_pipeline_process_frame(&pipe, acc, cnt, &frame);
        if ((frame.health_flags[0] & SEMG_HEALTH_STUCK_AT) && detected_at < 0) {
            detected_at = i + 1;
        }
    }
    assert(detected_at > 0 && detected_at <= 100);
    printf("[PASS] test_stuck_at (0 false positives on resting; real flatline detected at %d ms)\n", detected_at);
}

static void test_passband_125hz_250hz_no_boxcar_null(void)
{
    semg_pipeline_state_t pipe;
    semg_pipeline_init(&pipe, NULL);

    /* Inject 125 Hz and 250 Hz sine waves (where old 8-tap boxcar had complete spectral nulls) */
    const double freqs[2] = {125.0, 250.0};
    for (int f_idx = 0; f_idx < 2; f_idx++) {
        double max_out = 0.0;
        for (int n = 0; n < 200; n++) {
            double v_mv = 1650.0 + 113.6 * sin(2.0 * M_PI * freqs[f_idx] * (double)n / 1000.0);
            int32_t code_x20 = (int32_t)llround((v_mv / 3300.0) * 4095.0 * 20.0);
            int32_t acc[3] = {code_x20, code_x20, code_x20};
            uint16_t cnt[3] = {20, 20, 20};
            semg_processed_frame_t frame;
            semg_pipeline_process_frame(&pipe, acc, cnt, &frame);
            double abs_uv = fabs((double)frame.emg_rti_tenth_uv[0] * 0.1);
            if (abs_uv > max_out) max_out = abs_uv;
        }
        /* Expected peak ~1000 uV (allow +/- 2% tolerance) */
        assert(max_out > 980.0 && max_out < 1020.0);
        printf("[PASS] test_passband_%.0fHz (peak = %.1f uV, expected ~1000.0 uV)\n", freqs[f_idx], max_out);
    }
}

int main(void)
{
    printf("=== Running Firmware v3 Host Pipeline Unit Tests ===\n");
    test_lut_clamping();
    test_resolution_and_scaling();
    test_stuck_at_no_false_positive_on_resting();
    test_passband_125hz_250hz_no_boxcar_null();
    printf("=== ALL TESTS PASSED ===\n");
    return 0;
}

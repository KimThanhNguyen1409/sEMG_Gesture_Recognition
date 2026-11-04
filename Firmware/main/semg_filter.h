/**
 * @file semg_filter.h
 * @brief Zero-malloc Direct Form II Transposed IIR Biquad Notch Filter for 50 Hz powerline cancellation.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Biquad Direct Form II Transposed Filter state structure.
 */
typedef struct {
    float b0; /**< Normalized feedforward coefficient b0/a0 */
    float b1; /**< Normalized feedforward coefficient b1/a0 */
    float b2; /**< Normalized feedforward coefficient b2/a0 */
    float a1; /**< Normalized feedback coefficient a1/a0 */
    float a2; /**< Normalized feedback coefficient a2/a0 */
    float d1; /**< State delay register 1 */
    float d2; /**< State delay register 2 */
} semg_biquad_t;

/**
 * @brief Initialize a Biquad Notch filter with given parameters.
 *
 * @param[out] filter       Pointer to filter structure
 * @param[in]  sample_rate  Sampling rate in Hz (e.g. 1000.0f)
 * @param[in]  notch_freq   Target notch center frequency in Hz (e.g. 50.0f)
 * @param[in]  q            Quality factor Q (e.g. 5.0f for ~10 Hz notch bandwidth)
 */
void semg_filter_init(semg_biquad_t *filter, float sample_rate, float notch_freq, float q);

/**
 * @brief Process a single floating point sample through the Biquad filter.
 *
 * @param[in,out] filter Pointer to filter structure
 * @param[in]     input  Input sample in mV
 * @return float         Filtered output sample in mV
 */
float semg_filter_process(semg_biquad_t *filter, float input);

/**
 * @brief Reset the state delay registers of the filter (clears transient history).
 *
 * @param[in,out] filter Pointer to filter structure
 */
void semg_filter_reset(semg_biquad_t *filter);

#ifdef __cplusplus
}
#endif

/**
 * @file semg_filter.c
 * @brief Implementation of Direct Form II Transposed Biquad Notch Filter.
 */

#include "semg_filter.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void semg_filter_init(semg_biquad_t *filter, float sample_rate, float notch_freq, float q)
{
    if (filter == NULL || sample_rate <= 0.0f || notch_freq <= 0.0f || q <= 0.0f) {
        return;
    }

    /* Angular center frequency */
    float omega = (float)(2.0 * M_PI * (double)notch_freq / (double)sample_rate);
    float sin_omega = sinf(omega);
    float cos_omega = cosf(omega);
    float alpha = sin_omega / (2.0f * q);

    /* RBJ Audio EQ Cookbook Notch Filter Formulation:
     * b0 = 1
     * b1 = -2 * cos(omega)
     * b2 = 1
     * a0 = 1 + alpha
     * a1 = -2 * cos(omega)
     * a2 = 1 - alpha
     */
    float a0 = 1.0f + alpha;

    /* Normalize all coefficients by a0 */
    filter->b0 = 1.0f / a0;
    filter->b1 = (-2.0f * cos_omega) / a0;
    filter->b2 = 1.0f / a0;
    filter->a1 = (-2.0f * cos_omega) / a0;
    filter->a2 = (1.0f - alpha) / a0;

    /* Clear state history */
    filter->d1 = 0.0f;
    filter->d2 = 0.0f;
}

float semg_filter_process(semg_biquad_t *filter, float input)
{
    if (filter == NULL) {
        return input;
    }

    /* Direct Form II Transposed Difference Equations */
    float output = (filter->b0 * input) + filter->d1;
    filter->d1 = (filter->b1 * input) - (filter->a1 * output) + filter->d2;
    filter->d2 = (filter->b2 * input) - (filter->a2 * output);

    return output;
}

void semg_filter_reset(semg_biquad_t *filter)
{
    if (filter != NULL) {
        filter->d1 = 0.0f;
        filter->d2 = 0.0f;
    }
}

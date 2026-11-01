#ifndef SEMG_FEATURES_H
#define SEMG_FEATURES_H

#include "model_params.h"
#include <math.h>
#include <stdint.h>

#define SEMG_NUM_CHANNELS 3
#define SEMG_FEATURES_PER_CH 5
#define SEMG_TOTAL_FEATURES 15
#define SEMG_WINDOW_SIZE 200
#define SEMG_NUM_CLASSES 4

static const char* const SEMG_GESTURE_LABELS[SEMG_NUM_CLASSES] = {"RESTING", "FIST", "OPEN_HAND", "POINTING"};

static inline void semg_extract_15_features(const float window[SEMG_WINDOW_SIZE][SEMG_NUM_CHANNELS],
                                            float out_features[SEMG_TOTAL_FEATURES])
{
    const float thresh = 1e-5f;
    int idx            = 0;

    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++)
    {
        float sum     = 0.0f;
        float sum_abs = 0.0f;
        float sum_sq  = 0.0f;
        float wl      = 0.0f;
        int zc        = 0;
        int ssc       = 0;

        for (int i = 0; i < SEMG_WINDOW_SIZE; i++)
        {
            sum += window[i][ch];
        }
        float mean = sum / (float)SEMG_WINDOW_SIZE;

        for (int i = 0; i < SEMG_WINDOW_SIZE; i++)
        {
            float x = window[i][ch];
            sum_abs += fabsf(x);
            sum_sq += x * x;

            if (i > 0)
            {
                float prev_x = window[i - 1][ch];
                float diff   = x - prev_x;
                wl += fabsf(diff);

                float xc_prev = prev_x - mean;
                float xc_curr = x - mean;
                if (((xc_prev * xc_curr) < 0.0f) && (fabsf(diff) >= thresh))
                {
                    zc++;
                }

                if (i < SEMG_WINDOW_SIZE - 1)
                {
                    float next_x = window[i + 1][ch];
                    float diff1  = x - prev_x;
                    float diff2  = x - next_x;
                    if (((diff1 * diff2) > 0.0f) && ((fabsf(diff1) >= thresh) || (fabsf(diff2) >= thresh)))
                    {
                        ssc++;
                    }
                }
            }
        }

        // MAV
        out_features[idx++] = sum_abs / (float)SEMG_WINDOW_SIZE;
        // RMS
        out_features[idx++] = sqrtf(sum_sq / (float)SEMG_WINDOW_SIZE);
        // WL
        out_features[idx++] = wl;
        // ZC
        out_features[idx++] = (float)zc;
        // SSC
        out_features[idx++] = (float)ssc;
    }
}

static inline void semg_normalize_features(const float raw_features[SEMG_TOTAL_FEATURES],
                                           float norm_features[SEMG_TOTAL_FEATURES])
{
    for (int i = 0; i < SEMG_TOTAL_FEATURES; i++)
    {
        norm_features[i] = (raw_features[i] - FEATURE_MEAN[i]) / FEATURE_STD[i];
    }
}

#endif

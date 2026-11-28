/**
 * @file semg_adc_mock.c
 * @brief HAL Mock implementation of semg_adc.h for TESTING mode (no PCB required).
 *
 * Improvements:
 * - Ping-Pong Mirror playback (0 -> N-1 -> 0) eliminates 1-second loop boundary spikes.
 * - 64-sample (64 ms) integer cross-fade on BOOT button press suppresses IIR step ringing.
 * - Replays real 12-bit ADC waveforms from unseen Test-split trials (semg_mock_data.h).
 *
 * Build with: build.bat testing  (or idf.py -DFIRMWARE_MODE=TESTING build)
 */

#include "semg_adc.h"

#ifdef SEMG_MODE_SIMULATE

#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "semg_mock_data.h"

static const char *TAG = "SEMG_ADC_MOCK";

#define SEMG_FADE_STEPS (64u) /* 64 ms smooth integer cross-fade (>> 6) */

static TaskHandle_t       s_worker_task  = NULL;
static esp_timer_handle_t s_mock_timer   = NULL;
static volatile uint32_t  s_phase_idx    = 0;
static volatile uint8_t   s_gesture_mode = 0;
static volatile bool      s_frame_ready  = false;
static uint32_t           s_last_btn_ms  = 0;

static int16_t            s_last_code[SEMG_NUM_CHANNELS] = {2048, 2048, 2048};
static int16_t            s_fade_from[SEMG_NUM_CHANNELS] = {2048, 2048, 2048};
static uint8_t            s_fade_step    = SEMG_FADE_STEPS;

static const char * const GESTURE_NAMES[SEMG_MOCK_NUM_GESTURES] = {
    "RESTING", "FIST", "OPEN_HAND", "POINTING"
};

/* Lightweight deterministic LCG PRNG for realistic +/- 1 LSB ADC quantization dither */
static uint32_t s_rng = 0x12345678u;
static inline int16_t next_dither_lsb(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (int16_t)(((int32_t)((s_rng >> 24) % 3u)) - 1); /* -1, 0, +1 LSB */
}

static void mock_timer_cb(void *arg)
{
    (void)arg;
    s_frame_ready = true;
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }
}

int semg_adc_map_channel(uint8_t hardware_chan)
{
    if (hardware_chan == SEMG_CH1_ADC_CHANNEL) {
        return 0;
    } else if (hardware_chan == SEMG_CH2_ADC_CHANNEL) {
        return 1;
    } else if (hardware_chan == SEMG_CH3_ADC_CHANNEL) {
        return 2;
    }
    return -1;
}

adc_cali_handle_t semg_adc_get_cali_handle(void)
{
    return NULL;
}

esp_err_t semg_adc_dma_init(TaskHandle_t worker_task_to_notify)
{
    s_worker_task = worker_task_to_notify;

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << SEMG_DEMO_BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    const esp_timer_create_args_t timer_args = {
        .callback        = mock_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "semg_mock_1khz",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_mock_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create mock 1 kHz timer (%s)", esp_err_to_name(ret));
        return ret;
    }

    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
        s_last_code[ch] = SEMG_MOCK_WAVE[0][0][ch];
        s_fade_from[ch] = s_last_code[ch];
    }

    ESP_LOGW(TAG, "=== SIMULATE MODE ACTIVE: Ping-Pong Unseen Test-Split Data ===");
    ESP_LOGI(TAG, "Press BOOT (GPIO %d) to switch gesture. Current: %s (%s)",
             SEMG_DEMO_BTN_GPIO, GESTURE_NAMES[s_gesture_mode], SEMG_MOCK_SOURCE_FILES[s_gesture_mode]);
    return ESP_OK;
}

esp_err_t semg_adc_dma_start(void)
{
    if (s_mock_timer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_timer_start_periodic(s_mock_timer, 1000); /* 1000 us = 1 kHz */
}

esp_err_t semg_adc_dma_stop(void)
{
    if (s_mock_timer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_timer_stop(s_mock_timer);
}

esp_err_t semg_adc_dma_read_frame(uint8_t *out_dest, uint32_t *out_len)
{
    if (out_dest == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_frame_ready) {
        *out_len = 0;
        return ESP_ERR_TIMEOUT;
    }
    s_frame_ready = false;

    /* Debounced poll of BOOT button to switch unseen test gesture with smooth cross-fade */
    if (gpio_get_level(SEMG_DEMO_BTN_GPIO) == 0) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if ((now_ms - s_last_btn_ms) > 350u) {
            s_last_btn_ms = now_ms;
            for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
                s_fade_from[ch] = s_last_code[ch];
            }
            s_fade_step = 0;
            s_gesture_mode = (uint8_t)((s_gesture_mode + 1u) % SEMG_MOCK_NUM_GESTURES);
            s_phase_idx = 0;
        }
    }

    static const uint8_t hw_ch[SEMG_NUM_CHANNELS] = {
        SEMG_CH1_ADC_CHANNEL,
        SEMG_CH2_ADC_CHANNEL,
        SEMG_CH3_ADC_CHANNEL,
    };

    /* Ping-pong mirror indexing: 0 .. (N-1) .. 1 -> zero boundary discontinuity */
    const uint32_t period = 2u * (SEMG_MOCK_SEQ_LEN - 1u);
    const uint32_t phase  = s_phase_idx;
    s_phase_idx = (phase + 1u) % period;
    const uint32_t pos = (phase < SEMG_MOCK_SEQ_LEN) ? phase : (period - phase);

    const uint8_t mode = s_gesture_mode;
    int16_t cur_code[SEMG_NUM_CHANNELS];

    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
        int32_t target = (int32_t)SEMG_MOCK_WAVE[mode][pos][ch];
        if (s_fade_step < SEMG_FADE_STEPS) {
            int32_t w_new = (int32_t)s_fade_step;
            int32_t w_old = (int32_t)SEMG_FADE_STEPS - w_new;
            target = ((w_old * (int32_t)s_fade_from[ch]) + (w_new * target) + 32) >> 6;
        }
        cur_code[ch]  = (int16_t)target;
        s_last_code[ch] = (int16_t)target;
    }
    if (s_fade_step < SEMG_FADE_STEPS) {
        s_fade_step++;
    }

    /* Pack 20 oversamples x 3 channels = 60 Type-1 conversion results (120 bytes) */
    adc_digi_output_data_t *out = (adc_digi_output_data_t *)out_dest;
    int idx = 0;
    for (int ovs = 0; ovs < SEMG_OVERSAMPLE_RATIO; ovs++) {
        for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
            int32_t code = (int32_t)cur_code[ch] + next_dither_lsb();
            if (code < 0) code = 0;
            if (code > 4095) code = 4095;
            out[idx].val = 0;
            out[idx].type1.channel = hw_ch[ch];
            out[idx].type1.data = (uint16_t)code;
            idx++;
        }
    }

    *out_len = (uint32_t)(idx * sizeof(adc_digi_output_data_t));
    return ESP_OK;
}

#endif /* SEMG_MODE_SIMULATE */

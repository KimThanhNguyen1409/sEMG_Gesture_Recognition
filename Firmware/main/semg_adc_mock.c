/**
 * @file semg_adc_mock.c
 * @brief HAL Mock implementation of semg_adc.h for PCB-less ESP32-S & QEMU simulation.
 *
 * Emulates 60 kHz Continuous ADC DMA (20x oversampling x 3 channels = 120-byte Type-1 frame
 * every 1 ms) using esp_timer. Press BOOT button (GPIO 0) on ESP32-S to cycle gestures:
 *   0: RESTING -> 1: FIST -> 2: OPEN_HAND -> 3: POINTING
 *
 * Enable via CMake: idf.py -DSEMG_MOCK_ADC=1 build
 */

#include "semg_adc.h"
#include <math.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SEMG_ADC_MOCK";

static TaskHandle_t      s_worker_task   = NULL;
static esp_timer_handle_t s_mock_timer   = NULL;
static volatile uint32_t s_sample_idx    = 0;
static volatile uint8_t  s_gesture_mode  = 0;
static volatile bool     s_frame_ready   = false;
static uint32_t          s_last_btn_ms   = 0;

static const char *GESTURE_NAMES[4] = {"RESTING", "FIST", "OPEN_HAND", "POINTING"};

/* Channel amplitude profiles (in ADC counts peak around mid-rail 2048) per gesture:
 * [gesture][channel]: Ch1 (FCR), Ch2 (EDC), Ch3 (Brachioradialis) */
static const int16_t GESTURE_AMP[4][SEMG_NUM_CHANNELS] = {
    {   4,   4,   4 },  /* RESTING:   low baseline noise (~2-4 LSB) */
    { 320, 210, 260 },  /* FIST:      strong FCR + Brachioradialis + moderate EDC */
    {  70, 340, 180 },  /* OPEN_HAND: strong EDC extensor dominance */
    { 240, 120,  60 },  /* POINTING:  FCR dominant, low Brachioradialis */
};

/* Distinct spectral carriers (Hz) inside 20-450 Hz sEMG band */
static const float CH_FREQ_A[SEMG_NUM_CHANNELS] = { 73.0f, 113.0f, 157.0f };
static const float CH_FREQ_B[SEMG_NUM_CHANNELS] = { 191.0f, 241.0f, 293.0f };

/* Simple fast LCG PRNG for deterministic noise (zero malloc, ISR safe) */
static uint32_t s_rng = 0x12345678u;
static inline int16_t next_noise_lsb(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (int16_t)((int32_t)((s_rng >> 24) & 0x07u) - 3); /* -3..+4 LSB */
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
    /* NULL forces semg_pipeline.c to use exact int64_t linear ratio (0..3300 mV) */
    return NULL;
}

esp_err_t semg_adc_dma_init(TaskHandle_t worker_task_to_notify)
{
    s_worker_task = worker_task_to_notify;

    /* Configure BOOT button (GPIO 0) as input with pull-up for live gesture cycling */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << SEMG_DEMO_BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    const esp_timer_create_args_t timer_args = {
        .callback = mock_timer_cb,
        .arg      = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name     = "semg_mock_1khz",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_mock_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create mock 1 kHz timer (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGW(TAG, "=== MOCK ADC MODE ACTIVE (No PCB required) ===");
    ESP_LOGI(TAG, "Press BOOT (GPIO %d) to cycle gestures. Current: %s",
             SEMG_DEMO_BTN_GPIO, GESTURE_NAMES[s_gesture_mode]);
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

    /* Debounced poll of BOOT button to cycle gesture mode live in class */
    if (gpio_get_level(SEMG_DEMO_BTN_GPIO) == 0) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if ((now_ms - s_last_btn_ms) > 350u) {
            s_last_btn_ms = now_ms;
            s_gesture_mode = (uint8_t)((s_gesture_mode + 1u) % 4u);
            ESP_LOGI(TAG, "[DEMO] Switched simulated gesture -> %s", GESTURE_NAMES[s_gesture_mode]);
        }
    }

    static const uint8_t hw_ch[SEMG_NUM_CHANNELS] = {
        SEMG_CH1_ADC_CHANNEL,
        SEMG_CH2_ADC_CHANNEL,
        SEMG_CH3_ADC_CHANNEL,
    };

    const uint8_t mode = s_gesture_mode;
    const float t_sec = (float)s_sample_idx * 0.001f;
    s_sample_idx++;

    /* Precompute 1 kHz base sample per channel around mid-rail 2048 (1.65 V) */
    int16_t base_code[SEMG_NUM_CHANNELS];
    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
        float wave = 0.65f * sinf(6.2831853f * CH_FREQ_A[ch] * t_sec) +
                     0.35f * sinf(6.2831853f * CH_FREQ_B[ch] * t_sec);
        int32_t val = 2048 + (int32_t)(wave * (float)GESTURE_AMP[mode][ch]);
        if (val < 20) val = 20;
        if (val > 4075) val = 4075;
        base_code[ch] = (int16_t)val;
    }

    /* Pack 20 oversamples x 3 channels = 60 Type-1 conversion results (120 bytes) */
    adc_digi_output_data_t *out = (adc_digi_output_data_t *)out_dest;
    int idx = 0;
    for (int ovs = 0; ovs < SEMG_OVERSAMPLE_RATIO; ovs++) {
        for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
            int32_t code = (int32_t)base_code[ch] + next_noise_lsb();
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

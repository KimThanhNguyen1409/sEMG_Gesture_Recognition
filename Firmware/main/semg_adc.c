/**
 * @file semg_adc.c
 * @brief Continuous ADC DMA implementation with Ping-Pong Buffering and Crosstalk Immunity.
 */

#include "semg_adc.h"
#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "SEMG_ADC_DMA";

/* ADC Driver & Calibration Handles */
static adc_continuous_handle_t s_adc_handle   = NULL;
static adc_cali_handle_t       s_cali_handle  = NULL;
static bool                    s_cali_enabled = false;

/* ========================================================================= */
/*              MEMORY ALIGNED PING-PONG (DOUBLE) BUFFERS                    */
/* ========================================================================= */

/**
 * @brief 32-bit (Word) Aligned Ping-Pong buffers to prevent bus misalignments
 * and maintain strict cache coherency between DMA transfer and task processing.
 */
static WORD_ALIGNED_ATTR uint8_t s_ping_pong_buffer[2][SEMG_DMA_FRAME_SIZE];
static volatile uint8_t          s_active_buf_idx = 0;

/* ========================================================================= */
/*                        ISR EVENT CALLBACK (NO FPU)                        */
/* ========================================================================= */

/**
 * @brief DMA Conversion Complete ISR callback.
 *
 * CRITICAL RULE:
 * Absolutely NO floating-point operations (FPU) in this ISR.
 * Only basic integer manipulation and FreeRTOS task notification.
 */
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle,
                                     const adc_continuous_evt_data_t *edata,
                                     void *user_data)
{
    (void)handle;
    (void)edata;
    BaseType_t high_task_awoken = pdFALSE;
    TaskHandle_t worker_task = (TaskHandle_t)user_data;

    if (worker_task != NULL) {
        vTaskNotifyGiveFromISR(worker_task, &high_task_awoken);
    }
    return (high_task_awoken == pdTRUE);
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
    return s_cali_enabled ? s_cali_handle : NULL;
}

esp_err_t semg_adc_dma_init(TaskHandle_t worker_task_to_notify)
{
    ESP_LOGI(TAG, "Initializing Continuous ADC DMA for 3-CH sEMG at %d Hz...", SEMG_DMA_SAMPLE_FREQ_HZ);

    /* 1. Allocate Driver Handle with DMA Ring Pool */
    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = SEMG_DMA_MAX_STORE_BUF_SIZE,
        .conv_frame_size    = SEMG_DMA_FRAME_SIZE,
        .flags.flush_pool   = 1,
    };

    esp_err_t ret = adc_continuous_new_handle(&adc_handle_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create continuous ADC handle (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Configure 3-Channel Scanning Pattern on ADC1 */
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SEMG_DMA_SAMPLE_FREQ_HZ,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num    = SEMG_NUM_CHANNELS,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};

    /* Channel 1: GPIO 36 (SENSOR_VP / ADC1_CHANNEL_0) */
    adc_pattern[0].atten     = SEMG_ADC_ATTEN;
    adc_pattern[0].channel   = SEMG_CH1_ADC_CHANNEL & 0x7;
    adc_pattern[0].unit      = ADC_UNIT_1;
    adc_pattern[0].bit_width = SEMG_ADC_BITWIDTH;

    /* Channel 2: GPIO 39 (SENSOR_VN / ADC1_CHANNEL_3) */
    adc_pattern[1].atten     = SEMG_ADC_ATTEN;
    adc_pattern[1].channel   = SEMG_CH2_ADC_CHANNEL & 0x7;
    adc_pattern[1].unit      = ADC_UNIT_1;
    adc_pattern[1].bit_width = SEMG_ADC_BITWIDTH;

    /* Channel 3: GPIO 34 (IO34 / ADC1_CHANNEL_6) */
    adc_pattern[2].atten     = SEMG_ADC_ATTEN;
    adc_pattern[2].channel   = SEMG_CH3_ADC_CHANNEL & 0x7;
    adc_pattern[2].unit      = ADC_UNIT_1;
    adc_pattern[2].bit_width = SEMG_ADC_BITWIDTH;

    dig_cfg.adc_pattern = adc_pattern;

    ret = adc_continuous_config(s_adc_handle, &dig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set continuous ADC config (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* 3. Register Callbacks */
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ret = adc_continuous_register_event_callbacks(s_adc_handle, &cbs, (void *)worker_task_to_notify);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DMA event callbacks (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* 4. Initialize eFuse Calibration Scheme */
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id   = ADC_UNIT_1,
        .atten     = SEMG_ADC_ATTEN,
        .bitwidth  = SEMG_ADC_BITWIDTH,
#if CONFIG_IDF_TARGET_ESP32
        .default_vref = 1100,
#endif
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC Line-Fitting eFuse Calibration active");
    } else {
        ESP_LOGW(TAG, "ADC Calibration failed (%s). Fallback to linear ratio.", esp_err_to_name(ret));
        s_cali_enabled = false;
    }
#endif

    ESP_LOGI(TAG, "Continuous ADC DMA initialized. Channel Switching Settling Time: 16.67 us (> 20x tau)");
    return ESP_OK;
}

esp_err_t semg_adc_dma_read_frame(uint8_t *out_dest, uint32_t *out_len)
{
    if (s_adc_handle == NULL || out_dest == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t read_idx = s_active_buf_idx;
    uint32_t bytes_read = 0;

    /* Read from DMA into current active ping-pong buffer */
    esp_err_t ret = adc_continuous_read(s_adc_handle,
                                       s_ping_pong_buffer[read_idx],
                                       SEMG_DMA_FRAME_SIZE,
                                       &bytes_read,
                                       0);

    if (ret == ESP_OK && bytes_read > 0) {
        /* Copy to output destination */
        memcpy(out_dest, s_ping_pong_buffer[read_idx], bytes_read);
        *out_len = bytes_read;

        /* Flip active buffer index for strict Ping-Pong isolation */
        s_active_buf_idx = (read_idx ^ 1);
        return ESP_OK;
    }

    *out_len = 0;
    return ret;
}

esp_err_t semg_adc_dma_start(void)
{
    if (s_adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return adc_continuous_start(s_adc_handle);
}

esp_err_t semg_adc_dma_stop(void)
{
    if (s_adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return adc_continuous_stop(s_adc_handle);
}

/**
 * @file semg_adc.c
 * @brief Implementation of hardware ADC1 Oneshot and GPTimer for 1 kHz deterministic sampling.
 */

#include "semg_adc.h"
#include "esp_log.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "SEMG_ADC";

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_enabled = false;
static gptimer_handle_t          s_gptimer = NULL;

static const adc_channel_t s_channels[SEMG_NUM_CHANNELS] = {
    SEMG_CH1_ADC_CHANNEL,
    SEMG_CH2_ADC_CHANNEL,
    SEMG_CH3_ADC_CHANNEL,
};

static bool IRAM_ATTR semg_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)timer;
    (void)edata;
    BaseType_t high_task_awoken = pdFALSE;
    TaskHandle_t task = (TaskHandle_t)user_ctx;

    if (task != NULL) {
        vTaskNotifyGiveFromISR(task, &high_task_awoken);
    }
    return (high_task_awoken == pdTRUE);
}

esp_err_t semg_adc_init(void)
{
    ESP_LOGI(TAG, "Initializing ADC1 unit for sEMG 3-channel input...");

    /* 1. Initialize ADC1 Unit */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC1 unit (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Configure individual ADC1 channels */
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = SEMG_ADC_BITWIDTH,
        .atten = SEMG_ADC_ATTEN,
    };

    for (int i = 0; i < SEMG_NUM_CHANNELS; i++) {
        ret = adc_oneshot_config_channel(s_adc1_handle, s_channels[i], &chan_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure ADC1 channel %d (%s)", s_channels[i], esp_err_to_name(ret));
            return ret;
        }
    }

    /* 3. Initialize eFuse Calibration Scheme */
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = SEMG_ADC_ATTEN,
        .bitwidth = SEMG_ADC_BITWIDTH,
#if CONFIG_IDF_TARGET_ESP32
        .default_vref = 1100,
#endif
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC Line-Fitting eFuse Calibration active");
    } else {
        ESP_LOGW(TAG, "ADC Calibration unavailable (%s). Falling back to ratio approximation.", esp_err_to_name(ret));
        s_cali_enabled = false;
    }
#elif ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = s_channels[0],
        .atten = SEMG_ADC_ATTEN,
        .bitwidth = SEMG_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC Curve-Fitting Calibration active");
    } else {
        s_cali_enabled = false;
    }
#else
    s_cali_enabled = false;
#endif

    ESP_LOGI(TAG, "ADC1 initialized: CH1 (GPIO %d), CH2 (GPIO %d), CH3 (GPIO %d)",
             SEMG_CH1_GPIO, SEMG_CH2_GPIO, SEMG_CH3_GPIO);
    return ESP_OK;
}

esp_err_t semg_adc_read_all(int16_t out_mv[SEMG_NUM_CHANNELS], int16_t out_raw[SEMG_NUM_CHANNELS])
{
    if (s_adc1_handle == NULL || out_mv == NULL || out_raw == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < SEMG_NUM_CHANNELS; i++) {
        int raw_val = 0;
        esp_err_t ret = adc_oneshot_read(s_adc1_handle, s_channels[i], &raw_val);
        if (ret != ESP_OK) {
            return ret;
        }
        out_raw[i] = (int16_t)raw_val;

        if (s_cali_enabled && s_cali_handle != NULL) {
            int voltage = 0;
            ret = adc_cali_raw_to_voltage(s_cali_handle, raw_val, &voltage);
            if (ret == ESP_OK) {
                out_mv[i] = (int16_t)voltage;
            } else {
                out_mv[i] = (int16_t)((raw_val * 3300) / 4095);
            }
        } else {
            /* Ratio approximation for 12-bit ADC at 3.3V attenuation */
            out_mv[i] = (int16_t)((raw_val * 3300) / 4095);
        }
    }

    return ESP_OK;
}

esp_err_t semg_timer_init(TaskHandle_t task_to_notify)
{
    ESP_LOGI(TAG, "Configuring Hardware GPTimer for %d Hz deterministic alarm...", SEMG_SAMPLE_RATE_HZ);

    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = SEMG_TIMER_RESOLUTION_HZ,
    };
    esp_err_t ret = gptimer_new_timer(&timer_config, &s_gptimer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create gptimer (%s)", esp_err_to_name(ret));
        return ret;
    }

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = SEMG_TIMER_PERIOD_US,
        .flags.auto_reload_on_alarm = true,
    };

    gptimer_event_callbacks_t cbs = {
        .on_alarm = semg_timer_on_alarm_cb,
    };

    ret = gptimer_register_event_callbacks(s_gptimer, &cbs, (void *)task_to_notify);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register timer callbacks (%s)", esp_err_to_name(ret));
        return ret;
    }

    ret = gptimer_set_alarm_action(s_gptimer, &alarm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set alarm action (%s)", esp_err_to_name(ret));
        return ret;
    }

    ret = gptimer_enable(s_gptimer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable gptimer (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GPTimer successfully enabled (period: %d us)", SEMG_TIMER_PERIOD_US);
    return ESP_OK;
}

esp_err_t semg_timer_start(void)
{
    if (s_gptimer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return gptimer_start(s_gptimer);
}

esp_err_t semg_timer_stop(void)
{
    if (s_gptimer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return gptimer_stop(s_gptimer);
}

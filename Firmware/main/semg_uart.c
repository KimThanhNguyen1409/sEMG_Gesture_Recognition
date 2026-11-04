/**
 * @file semg_uart.c
 * @brief Implementation of UART high-speed streaming driver.
 */

#include "semg_uart.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "SEMG_UART";

esp_err_t semg_uart_init(void)
{
    ESP_LOGI(TAG, "Initializing UART%d with baudrate %d...", SEMG_UART_PORT, SEMG_UART_BAUDRATE);

    uart_config_t uart_config = {
        .baud_rate = SEMG_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Only install driver if not already installed */
    if (!uart_is_driver_installed(SEMG_UART_PORT)) {
        esp_err_t ret = uart_driver_install(SEMG_UART_PORT, 256, SEMG_UART_TX_BUF_SIZE, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "uart_driver_install failed (%s)", esp_err_to_name(ret));
            return ret;
        }
    }

    esp_err_t ret = uart_param_config(SEMG_UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART initialized successfully for sEMG CSV streaming");
    return ESP_OK;
}

void semg_uart_send_sample(const semg_sample_t *sample)
{
    if (sample == NULL) {
        return;
    }

    /* Stack allocated buffer - Zero dynamic malloc */
    char buf[80];
    int len = 0;

#if (ENABLE_DSP_FILTERING == 1)
    /* Output filtered and DC-centered signal in mV */
    len = snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f\r\n",
                   sample->filtered[0],
                   sample->filtered[1],
                   sample->filtered[2]);
#else
    /* Output True Raw Calibrated millivolts */
    len = snprintf(buf, sizeof(buf), "%d,%d,%d\r\n",
                   (int)sample->voltage_mv[0],
                   (int)sample->voltage_mv[1],
                   (int)sample->voltage_mv[2]);
#endif

    if (len > 0 && len < (int)sizeof(buf)) {
        uart_write_bytes(SEMG_UART_PORT, buf, (size_t)len);
    }
}

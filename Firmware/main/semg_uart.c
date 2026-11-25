/**
 * @file semg_uart.c
 * @brief Implementation of UART high-speed (921600 baud) streaming driver for v3 sEMG data.
 */

#include "semg_uart.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "SEMG_UART";

esp_err_t semg_uart_init(void)
{
    ESP_LOGI(TAG, "Initializing UART%d at %d baud...", SEMG_UART_PORT, SEMG_UART_BAUDRATE);

    uart_config_t uart_config = {
        .baud_rate  = SEMG_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

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

    ESP_LOGI(TAG, "UART initialized successfully (v3 0.1 uV stream @ %d baud)", SEMG_UART_BAUDRATE);
    return ESP_OK;
}

void semg_uart_send_frame(const semg_processed_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    /* Stack allocated buffer - Zero dynamic allocation */
    char buf[80];
    int len = 0;

#if (SEMG_UART_OUTPUT_RTI_TENTH_UV == 1)
    /* Output v3 Physical Referred-To-Input potential in 0.1 uV integer units (10 = 1.0 uV) */
    len = snprintf(buf, sizeof(buf), "%ld,%ld,%ld\r\n",
                   (long)frame->emg_rti_tenth_uv[0],
                   (long)frame->emg_rti_tenth_uv[1],
                   (long)frame->emg_rti_tenth_uv[2]);
#else
    /* Output Linearized & Calibrated millivolts at pin (mV) */
    len = snprintf(buf, sizeof(buf), "%d,%d,%d\r\n",
                   (int)frame->voltage_mv[0],
                   (int)frame->voltage_mv[1],
                   (int)frame->voltage_mv[2]);
#endif

    if (len > 0 && len < (int)sizeof(buf)) {
        uart_write_bytes(SEMG_UART_PORT, buf, (size_t)len);
    }
}

/**
 * @file main.c
 * @brief Main entry point for ESP32-S 3-Channel sEMG Data Acquisition Firmware.
 *
 * Architecture:
 * - Deterministic 1 kHz Sampling via Hardware GPTimer Alarm ISR -> Direct Task Notification.
 * - Producer Task (Pinned to Core 0): Reads ADC1 channels (GPIO 34, 35, 32), runs 50 Hz Biquad Notch DSP.
 * - Zero-malloc FreeRTOS Static Queue.
 * - Consumer Task (Pinned to Core 1): Formats CSV stream and pushes over High-Speed UART0.
 *
 * Compliance:
 * - MISRA-C / Embedded Safety: Zero heap allocation in runtime, full NULL and error code checking.
 */

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "semg_config.h"
#include "semg_adc.h"
#include "semg_filter.h"
#include "semg_uart.h"

static const char *TAG = "SEMG_APP";

/* ========================================================================= */
/*                       STATIC MEMORY ALLOCATION                            */
/* ========================================================================= */

/* Static Queue Storage */
static StaticQueue_t s_queue_buf;
static uint8_t       s_queue_storage[SEMG_QUEUE_LENGTH * sizeof(semg_sample_t)];
static QueueHandle_t s_sample_queue = NULL;

/* Static Sampler Task (Core 0) */
static StaticTask_t  s_sampler_task_buf;
static StackType_t   s_sampler_task_stack[SEMG_SAMPLER_STACK_SIZE];
static TaskHandle_t  s_sampler_task_handle = NULL;

/* Static UART Task (Core 1) */
static StaticTask_t  s_uart_task_buf;
static StackType_t   s_uart_task_stack[SEMG_UART_STACK_SIZE];
static TaskHandle_t  s_uart_task_handle = NULL;

/* Performance Counters */
static volatile uint32_t s_samples_acquired = 0;
static volatile uint32_t s_dropped_samples  = 0;

/* ========================================================================= */
/*                          CORE 0: SAMPLER TASK                             */
/* ========================================================================= */

static void semg_sampler_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Sampler Task started on Core %d (Priority %d)", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    /* Initialize 3 independent Biquad Notch filters for 3 sEMG channels */
    semg_biquad_t notch_filters[SEMG_NUM_CHANNELS];
    for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
        semg_filter_init(&notch_filters[ch],
                         (float)SEMG_SAMPLE_RATE_HZ,
                         SEMG_NOTCH_FREQ_HZ,
                         SEMG_NOTCH_Q);
    }

    semg_sample_t current_sample;

    while (1) {
        /* Block until woken up by Hardware GPTimer alarm ISR (< 1 us jitter) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        s_samples_acquired++;
        current_sample.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        /* Read 3-channel ADC with eFuse calibration */
        esp_err_t ret = semg_adc_read_all(current_sample.voltage_mv, current_sample.raw_adc);
        if (ret == ESP_OK) {
            for (int ch = 0; ch < SEMG_NUM_CHANNELS; ch++) {
#if (ENABLE_DSP_FILTERING == 1)
                /* Subtract Virtual Ground DC bias (1.65V = 1650 mV) */
                float ac_signal = (float)current_sample.voltage_mv[ch] - SEMG_DC_OFFSET_MV;

                /* 50 Hz Biquad Notch Filter processing */
                current_sample.filtered[ch] = semg_filter_process(&notch_filters[ch], ac_signal);
#else
                current_sample.filtered[ch] = (float)current_sample.voltage_mv[ch];
#endif
            }

            /* Push to FreeRTOS Queue (non-blocking) */
            if (xQueueSend(s_sample_queue, &current_sample, 0) != pdPASS) {
                s_dropped_samples++;
            }
        }
    }
}

/* ========================================================================= */
/*                           CORE 1: UART TASK                               */
/* ========================================================================= */

static void semg_uart_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "UART Task started on Core %d (Priority %d)", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    semg_sample_t sample_to_send;

    while (1) {
        /* Block until a new sample arrives from the Sampler task */
        if (xQueueReceive(s_sample_queue, &sample_to_send, portMAX_DELAY) == pdPASS) {
            semg_uart_send_sample(&sample_to_send);
        }
    }
}

/* ========================================================================= */
/*                              APP MAIN ENTRY                               */
/* ========================================================================= */

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  ESP32-S 3-CH sEMG RAW DATA ACQUISITION FIRMWARE         ");
    ESP_LOGI(TAG, "  Sampling: 1000 Hz | DSP Notch: 50 Hz | Transport: UART  ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Create Static Queue */
    s_sample_queue = xQueueCreateStatic(SEMG_QUEUE_LENGTH,
                                        sizeof(semg_sample_t),
                                        s_queue_storage,
                                        &s_queue_buf);
    if (s_sample_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create static sample queue!");
        return;
    }

    /* 2. Initialize Hardware Peripherals */
    ESP_ERROR_CHECK(semg_uart_init());
    ESP_ERROR_CHECK(semg_adc_init());

    /* 3. Create Static FreeRTOS Tasks */
    s_sampler_task_handle = xTaskCreateStaticPinnedToCore(
        semg_sampler_task,
        "semg_sampler",
        SEMG_SAMPLER_STACK_SIZE,
        NULL,
        SEMG_SAMPLER_PRIORITY,
        s_sampler_task_stack,
        &s_sampler_task_buf,
        SEMG_SAMPLER_CORE
    );
    if (s_sampler_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create static Sampler Task!");
        return;
    }

    s_uart_task_handle = xTaskCreateStaticPinnedToCore(
        semg_uart_task,
        "semg_uart",
        SEMG_UART_STACK_SIZE,
        NULL,
        SEMG_UART_PRIORITY,
        s_uart_task_stack,
        &s_uart_task_buf,
        SEMG_UART_CORE
    );
    if (s_uart_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create static UART Task!");
        return;
    }

    /* 4. Initialize and Start Hardware GPTimer */
    ESP_ERROR_CHECK(semg_timer_init(s_sampler_task_handle));
    ESP_ERROR_CHECK(semg_timer_start());

    ESP_LOGI(TAG, "System running. Stream active on UART%d (%d baud).", SEMG_UART_PORT, SEMG_UART_BAUDRATE);

    /* 5. Background Monitor Loop (Runs at idle priority in main task) */
    uint32_t last_sample_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t current_samples = s_samples_acquired;
        uint32_t delta_samples = current_samples - last_sample_count;
        last_sample_count = current_samples;

        float effective_rate = (float)delta_samples / 5.0f;
        UBaseType_t queue_spaces = uxQueueSpacesAvailable(s_sample_queue);

        ESP_LOGI(TAG, "[MONITOR] Rate: %.1f Hz | Queue Free: %u/%d | Dropped: %lu",
                 effective_rate, (unsigned int)queue_spaces, SEMG_QUEUE_LENGTH, (unsigned long)s_dropped_samples);
    }
}

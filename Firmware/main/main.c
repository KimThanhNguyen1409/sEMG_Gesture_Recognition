/**
 * @file main.c
 * @brief High-performance ESP32-S 3-Channel sEMG Data Acquisition System (v3).
 *
 * Supports 3 Build Modes:
 * - RELEASE  : Real PCB ADC1 DMA (GPIO 36, 39, 34), streams v3 0.1 uV RTI.
 * - TESTING  : Unseen Test-Split Flash Mock, IO0/BOOT button switches 4 gestures.
 * - SIMULATE : 5V Prototype Circuit + Voltage Divider on GPIO 34, 35, 32 (ADC1 CH6, 7, 4);
 *              starts in CHECK_RAW_MV (0..3300 mV) for waveform/VREF check, press IO0/BOOT
 *              to Auto-Tare DC and switch live into AI_LIVE_V3 (0.1 uV RTI) for AI Server!
 */

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "semg_config.h"
#include "semg_adc.h"
#include "semg_pipeline.h"
#include "semg_uart.h"
#if (SEMG_ENABLE_WIFI_UDP == 1)
#include "semg_wifi_udp.h"
#endif

static const char *TAG = "SEMG_MAIN";

/* ========================================================================= */
/*                       STATIC MEMORY ALLOCATION                            */
/* ========================================================================= */

static StaticQueue_t s_queue_buf;
static uint8_t       s_queue_storage[SEMG_QUEUE_LENGTH * sizeof(semg_processed_frame_t)];
static QueueHandle_t s_frame_queue = NULL;

static StaticTask_t  s_sampler_task_buf;
static StackType_t   s_sampler_task_stack[SEMG_SAMPLER_STACK_SIZE];
static TaskHandle_t  s_sampler_task_handle = NULL;

static StaticTask_t  s_uart_task_buf;
static StackType_t   s_uart_task_stack[SEMG_UART_STACK_SIZE];
static TaskHandle_t  s_uart_task_handle = NULL;

static semg_pipeline_state_t s_pipeline;

static volatile uint32_t s_frames_processed = 0;
static volatile uint32_t s_dropped_frames   = 0;

/* ========================================================================= */
/*                   CORE 0: SAMPLER & PIPELINE WORKER                       */
/* ========================================================================= */

static void semg_sampler_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Sampler Task started on Core %d (Priority %d)", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    WORD_ALIGNED_ATTR uint8_t raw_dma_frame[SEMG_DMA_FRAME_SIZE];
    uint32_t bytes_read = 0;

    semg_processed_frame_t processed_frame;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (semg_adc_dma_read_frame(raw_dma_frame, &bytes_read) == ESP_OK && bytes_read > 0) {
            int32_t ch_accum[SEMG_NUM_CHANNELS] = {0};
            uint16_t ch_count[SEMG_NUM_CHANNELS] = {0};

            for (uint32_t i = 0; i < bytes_read; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&raw_dma_frame[i];
                uint8_t chan = p->type1.channel;
                uint16_t val = p->type1.data;

                int ch_idx = semg_adc_map_channel(chan);
                if (ch_idx >= 0 && ch_idx < SEMG_NUM_CHANNELS) {
                    ch_accum[ch_idx] += val;
                    ch_count[ch_idx]++;
                }
            }

            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            processed_frame.output_rti_mode = SEMG_UART_OUTPUT_RTI_TENTH_UV;
            processed_frame.timestamp_ms = now_ms;
            semg_pipeline_process_frame(&s_pipeline, ch_accum, ch_count, &processed_frame);
            s_frames_processed++;

            if (xQueueSend(s_frame_queue, &processed_frame, 0) != pdPASS) {
                s_dropped_frames++;
            }
        }
    }
}

/* ========================================================================= */
/*                          CORE 1: UART STREAMER                            */
/* ========================================================================= */

static void semg_uart_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "UART Streamer Task started on Core %d (Priority %d)", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    semg_processed_frame_t frame_to_send;

    while (1) {
        if (xQueueReceive(s_frame_queue, &frame_to_send, portMAX_DELAY) == pdPASS) {
            semg_uart_send_frame(&frame_to_send);
#if (SEMG_ENABLE_WIFI_UDP == 1)
            semg_wifi_udp_push_frame(&frame_to_send);
            semg_wifi_udp_poll_uart_cmd();
#endif
        }
    }
}

/* ========================================================================= */
/*                               APP MAIN                                    */
/* ========================================================================= */

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  ESP32-S 3-CH sEMG DMA PIPELINE FIRMWARE (v3)            ");
    ESP_LOGI(TAG, "  Build Mode: %s", SEMG_MODE_STR);
    ESP_LOGI(TAG, "  DMA Rate: 60 kHz | Decimated: 1000 Hz | UART: %d baud", SEMG_UART_BAUDRATE);
    ESP_LOGI(TAG, "==========================================================");

#if (SEMG_USE_DIVIDER_AUTOTARE == 1)
    ESP_LOGI(TAG, "[RELEASE-5V] Divider R_TOP=%lld / R_BOT=%lld Ohm. Auto-Tare 500ms -> Direct AI_LIVE_V3 (0.1 uV RTI) over Wi-Fi UDP & UART!",
             (long long)SEMG_SIM_R_TOP_OHM, (long long)SEMG_SIM_R_BOT_OHM);
#endif

    s_frame_queue = xQueueCreateStatic(SEMG_QUEUE_LENGTH,
                                       sizeof(semg_processed_frame_t),
                                       s_queue_storage,
                                       &s_queue_buf);
    if (s_frame_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create static frame queue!");
        return;
    }

    ESP_ERROR_CHECK(semg_uart_init());

#if (SEMG_ENABLE_WIFI_UDP == 1)
    esp_err_t wifi_ret = semg_wifi_udp_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi UDP init failed (%s) - continuing on UART only", esp_err_to_name(wifi_ret));
    }
#endif

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

    ESP_ERROR_CHECK(semg_adc_dma_init(s_sampler_task_handle));
    semg_pipeline_init(&s_pipeline, semg_adc_get_cali_handle());
    ESP_ERROR_CHECK(semg_adc_dma_start());

    ESP_LOGI(TAG, "Continuous DMA Running @ %d baud", SEMG_UART_BAUDRATE);

    uint32_t last_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t cur = s_frames_processed;
        uint32_t delta = cur - last_count;
        last_count = cur;
        (void)delta;
#if (SEMG_ENABLE_PERIODIC_LOG == 1)
        float rate = (float)delta / 5.0f;
        UBaseType_t free_slots = uxQueueSpacesAvailable(s_frame_queue);

        ESP_LOGI(TAG, "[MONITOR] Output Rate: %.1f Hz | Queue Free: %u/%d | Dropped: %lu",
                 rate, (unsigned int)free_slots, SEMG_QUEUE_LENGTH, (unsigned long)s_dropped_frames);
#endif
    }
}

/**
 * @file main.c
 * @brief High-performance ESP32-S 3-Channel sEMG Data Acquisition System.
 *
 * Architecture Highlights:
 * - Continuous ADC DMA with Hardware Clock (60 kHz aggregate, 20 kHz/channel).
 * - Ping-Pong (Double) Buffering with Word-Aligned internal memory.
 * - ZERO Floating-Point math in ISRs (all integer arithmetic).
 * - Channel Switching Settling Time: 16.67 us (> 20x tau of RC anti-kickback filter) -> Zero Crosstalk.
 * - 6-Stage Preprocessing Pipeline:
 *   1. Validation (Range & Stuck-At Flatline Detection)
 *   2. Oversampling Decimation & Averaging (20:1 -> 1 kHz target rate)
 *   3. Boxcar FIR Moving Average (8-tap, power-of-2 shift)
 *   4. eFuse Calibration
 *   5. Piecewise Linearization Curve Compensation
 *   6. Scaling to Physical Units (Referred-To-Input uV & mV)
 * - FreeRTOS Dual-Core Producer-Consumer Architecture (Zero-malloc runtime).
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
#include "semg_pipeline.h"
#include "semg_uart.h"

static const char *TAG = "SEMG_MAIN";

/* ========================================================================= */
/*                       STATIC MEMORY ALLOCATION                            */
/* ========================================================================= */

/* Static Queue Storage */
static StaticQueue_t s_queue_buf;
static uint8_t       s_queue_storage[SEMG_QUEUE_LENGTH * sizeof(semg_processed_frame_t)];
static QueueHandle_t s_frame_queue = NULL;

/* Static Sampler Task (Core 0) */
static StaticTask_t  s_sampler_task_buf;
static StackType_t   s_sampler_task_stack[SEMG_SAMPLER_STACK_SIZE];
static TaskHandle_t  s_sampler_task_handle = NULL;

/* Static UART Task (Core 1) */
static StaticTask_t  s_uart_task_buf;
static StackType_t   s_uart_task_stack[SEMG_UART_STACK_SIZE];
static TaskHandle_t  s_uart_task_handle = NULL;

/* Performance Counters */
static volatile uint32_t s_frames_processed = 0;
static volatile uint32_t s_dropped_frames   = 0;

/* ========================================================================= */
/*                   CORE 0: SAMPLER & PIPELINE WORKER                       */
/* ========================================================================= */

static void semg_sampler_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Sampler Task started on Core %d (Priority %d)", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    /* Initialize 6-stage integer preprocessing pipeline */
    semg_pipeline_state_t pipeline;
    semg_pipeline_init(&pipeline, semg_adc_get_cali_handle());

    /* Memory-aligned local reception buffer */
    WORD_ALIGNED_ATTR uint8_t raw_dma_frame[SEMG_DMA_FRAME_SIZE];
    uint32_t bytes_read = 0;

    semg_processed_frame_t processed_frame;

    while (1) {
        /* Block until DMA conversion frame is ready (< 1 us ISR latency) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Read from DMA Ping-Pong buffer */
        while (semg_adc_dma_read_frame(raw_dma_frame, &bytes_read) == ESP_OK && bytes_read > 0) {
            int32_t ch_accum[SEMG_NUM_CHANNELS] = {0};
            uint16_t ch_count[SEMG_NUM_CHANNELS] = {0};

            /* Demultiplex 3 channels from DMA byte stream (Type1: 12-bit data + 4-bit chan) */
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

            /* Execute 6-stage zero-float preprocessing pipeline */
            processed_frame.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
            semg_pipeline_process_frame(&pipeline, ch_accum, ch_count, &processed_frame);
            s_frames_processed++;

            /* Push processed frame to FreeRTOS Queue (non-blocking) */
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

    /* 1. Create Static FreeRTOS Queue */
    s_frame_queue = xQueueCreateStatic(SEMG_QUEUE_LENGTH,
                                       sizeof(semg_processed_frame_t),
                                       s_queue_storage,
                                       &s_queue_buf);
    if (s_frame_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create static frame queue!");
        return;
    }

    /* 2. Initialize Hardware UART */
    ESP_ERROR_CHECK(semg_uart_init());

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

    /* 4. Initialize Continuous ADC DMA with Sampler Task Notification */
    ESP_ERROR_CHECK(semg_adc_dma_init(s_sampler_task_handle));

    /* 5. Start DMA Sampling Hardware */
    ESP_ERROR_CHECK(semg_adc_dma_start());

    ESP_LOGI(TAG, "Continuous DMA Running. Output: %s @ %d baud",
             SEMG_UART_OUTPUT_RTI_TENTH_UV ? "v3 RTI (0.1 uV)" : "Calibrated mV",
             SEMG_UART_BAUDRATE);

    /* 6. Background Monitor Loop */
    uint32_t last_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t cur = s_frames_processed;
        uint32_t delta = cur - last_count;
        last_count = cur;

        float rate = (float)delta / 5.0f;
        UBaseType_t free_slots = uxQueueSpacesAvailable(s_frame_queue);

        ESP_LOGI(TAG, "[MONITOR] Output Rate: %.1f Hz | Queue Free: %u/%d | Dropped: %lu",
                 rate, (unsigned int)free_slots, SEMG_QUEUE_LENGTH, (unsigned long)s_dropped_frames);
    }
}

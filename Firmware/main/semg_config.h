/**
 * @file semg_config.h
 * @brief System configuration, pin mapping, and hardware parameters for ESP32-S sEMG system.
 *
 * Target: ESP32-S (ESP32 Classic Xtensa LX6)
 * AFE: 3-Channel Op-Amp (MCP6004) with Virtual Ground Vref = 1.65V
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#include "hal/uart_types.h"
#include "soc/gpio_num.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                              HARDWARE PINS                                */
/* ========================================================================= */

/**
 * @brief ADC1 Channels on ESP32 Classic (ESP32-S / ESP32-D0WD).
 * These pins belong exclusively to ADC1 and are safe from Wi-Fi arbiter conflicts.
 *
 * Channel 1: GPIO 34 (ADC1_CHANNEL_6) -> sEMG Ch1 (e.g. FCR muscle)
 * Channel 2: GPIO 35 (ADC1_CHANNEL_7) -> sEMG Ch2 (e.g. EDC muscle)
 * Channel 3: GPIO 32 (ADC1_CHANNEL_4) -> sEMG Ch3 (e.g. Brachioradialis muscle)
 */
#define SEMG_NUM_CHANNELS           (3)

#define SEMG_CH1_GPIO               (GPIO_NUM_34)
#define SEMG_CH1_ADC_CHANNEL        (ADC_CHANNEL_6)

#define SEMG_CH2_GPIO               (GPIO_NUM_35)
#define SEMG_CH2_ADC_CHANNEL        (ADC_CHANNEL_7)

#define SEMG_CH3_GPIO               (GPIO_NUM_32)
#define SEMG_CH3_ADC_CHANNEL        (ADC_CHANNEL_4)

#define SEMG_ADC_ATTEN              (ADC_ATTEN_DB_12)
#define SEMG_ADC_BITWIDTH           (ADC_BITWIDTH_12)

/* ========================================================================= */
/*                          SAMPLING & TIMER PARAMS                          */
/* ========================================================================= */

/**
 * @brief Sampling rate set to 1000 Hz (1 ms period) as required by sEMG Nyquist criteria
 * (Bandwidth 20 - 450 Hz, Nyquist >= 900 Hz).
 */
#define SEMG_SAMPLE_RATE_HZ         (1000)
#define SEMG_TIMER_RESOLUTION_HZ    (1000000)  /* 1 MHz = 1 tick per us */
#define SEMG_TIMER_PERIOD_US        (1000)     /* 1000 us = 1 ms */

/* ========================================================================= */
/*                       DSP & FILTER CONFIGURATION                          */
/* ========================================================================= */

/**
 * @brief Enable/Disable DSP Preprocessing pipeline:
 * - 1: Biquad Notch 50 Hz Filter + DC Offset Subtraction (1.65V) enabled.
 * - 0: True Raw Calibrated mV (centered around 1650 mV) output.
 */
#define ENABLE_DSP_FILTERING        (1)

#define SEMG_NOTCH_FREQ_HZ          (50.0f)    /* Vietnam / Asian powerline frequency */
#define SEMG_NOTCH_Q                (5.0f)     /* Quality factor (bandwidth ~ 10 Hz) */
#define SEMG_DC_OFFSET_MV           (1650.0f)  /* AFE Virtual Ground VREF (3.3V / 2) */

/* ========================================================================= */
/*                      UART OUTPUT CONFIGURATION                            */
/* ========================================================================= */

#define SEMG_UART_PORT              (UART_NUM_0)
#define SEMG_UART_BAUDRATE          (115200)   /* Compatible with Arduino Plotter / Teleplot */
#define SEMG_UART_TX_BUF_SIZE       (1024)

/* ========================================================================= */
/*                      FREERTOS & MEMORY PARAMS                             */
/* ========================================================================= */

#define SEMG_QUEUE_LENGTH           (128)      /* Number of sample slots in static queue */
#define SEMG_SAMPLER_STACK_SIZE     (4096)
#define SEMG_UART_STACK_SIZE        (4096)

#define SEMG_SAMPLER_PRIORITY       (10)       /* High priority for deterministic 1kHz sample */
#define SEMG_UART_PRIORITY          (5)        /* Normal priority for stream formatting */

#define SEMG_SAMPLER_CORE           (0)        /* Pinned to Core 0 */
#define SEMG_UART_CORE              (1)        /* Pinned to Core 1 */

/* ========================================================================= */
/*                           DATA STRUCTURES                                 */
/* ========================================================================= */

/**
 * @brief Single multi-channel sEMG sample packet.
 */
typedef struct {
    uint32_t timestamp_ms;                     /* Timestamp in ms */
    int16_t  raw_adc[SEMG_NUM_CHANNELS];       /* Raw 12-bit ADC counts (0 - 4095) */
    int16_t  voltage_mv[SEMG_NUM_CHANNELS];    /* Calibrated millivolts from eFuse (0 - 3300 mV) */
    float    filtered[SEMG_NUM_CHANNELS];      /* DSP filtered and DC-centered signal in mV */
} semg_sample_t;

#ifdef __cplusplus
}
#endif

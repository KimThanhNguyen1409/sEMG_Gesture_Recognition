/**
 * @file semg_config.h
 * @brief System configuration, pin mapping, DMA & v3 DSP parameters for ESP32-S sEMG system.
 *
 * Target: ESP32-S (ESP32 Classic Xtensa LX6, chip D0WD)
 * AFE: 3-Channel Op-Amp (MCP6004) with Virtual Ground Vref = 1.65V, Gain = 113.6x
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef HOST_SIM
#include "esp_adc/adc_continuous.h"
#include "hal/adc_types.h"
#include "hal/uart_types.h"
#include "soc/gpio_num.h"
#include "soc/soc_caps.h"
#else
/* Host simulation stubs for native GCC compilation */
typedef int esp_err_t;
#define ESP_OK                  (0)
#define ESP_FAIL                (-1)
#define GPIO_NUM_36             (36)
#define GPIO_NUM_39             (39)
#define GPIO_NUM_34             (34)
#define ADC_CHANNEL_0           (0)
#define ADC_CHANNEL_3           (3)
#define ADC_CHANNEL_6           (6)
#define ADC_ATTEN_DB_12         (3)
#define SOC_ADC_DIGI_MAX_BITWIDTH (12)
#define UART_NUM_0              (0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                       BUILD MODE: RELEASE vs TESTING                      */
/* ========================================================================= */

/**
 * @brief Select firmware operation mode at build time:
 * - SEMG_MODE_RELEASE: Real hardware ADC1 DMA (Pads 4, 5, 6 -> GPIO 36, 39, 34).
 * - SEMG_MODE_TESTING: HAL Mock ADC replaying unseen Test-split sEMG waveforms
 *                      from Flash (press BOOT GPIO 0 to cycle 4 gestures).
 *
 * Select via CMake / build.bat:
 *   build.bat release   -> -DFIRMWARE_MODE=RELEASE (default)
 *   build.bat testing   -> -DFIRMWARE_MODE=TESTING
 */
#if !defined(SEMG_MODE_TESTING) && !defined(SEMG_MODE_RELEASE)
#define SEMG_MODE_RELEASE              (1)
#endif

#ifdef SEMG_MODE_TESTING
#define SEMG_MODE_STR                  "TESTING (Mock Unseen Test-Split)"
#else
#define SEMG_MODE_STR                  "RELEASE (Real Hardware ADC1 DMA)"
#endif

/* ========================================================================= */
/*                              HARDWARE PINS                                */
/* ========================================================================= */

#define SEMG_NUM_CHANNELS              (3)

/* Channels on ADC1 matching the actual PCB schematic & layout (Pads 4, 5, 6) */
#define SEMG_CH1_GPIO                  (GPIO_NUM_36)
#define SEMG_CH1_ADC_CHANNEL           (ADC_CHANNEL_0) /* SENSOR_VP (Pad 4) -> ADC_CH1 */

#define SEMG_CH2_GPIO                  (GPIO_NUM_39)
#define SEMG_CH2_ADC_CHANNEL           (ADC_CHANNEL_3) /* SENSOR_VN (Pad 5) -> ADC_CH2 */

#define SEMG_CH3_GPIO                  (GPIO_NUM_34)
#define SEMG_CH3_ADC_CHANNEL           (ADC_CHANNEL_6) /* IO34      (Pad 6) -> ADC_CH3 */

#define SEMG_ADC_ATTEN                 (ADC_ATTEN_DB_12)
#define SEMG_ADC_BITWIDTH              (SOC_ADC_DIGI_MAX_BITWIDTH) /* 12-bit */

/* Demo/Mock gesture switch button (BOOT button on ESP32-S) */
#define SEMG_DEMO_BTN_GPIO             (0)

/* ========================================================================= */
/*                   DMA & CONTINUOUS SAMPLING ENGINE                        */
/* ========================================================================= */

/**
 * @brief Continuous ADC DMA Sampling Frequencies:
 * - Aggregate DMA Sample Frequency = 60,000 Hz (60 kHz)
 * - Per-channel sample rate = 60 kHz / 3 channels = 20,000 Hz (20 kHz)
 * - Oversampling Factor = 20 (first sinc null at 1000 Hz, preserves 20-450 Hz sEMG band)
 * - Decimated Output Rate = 20,000 Hz / 20 = 1,000 Hz (1 kHz = 1 ms per frame)
 *
 * Settling & Crosstalk Immunity:
 * Time between channel switching = 1 / 60 kHz = 16.67 us.
 * Hardware anti-kickback filter: R = 100 ohm, C = 1 nF -> tau = 100 ns.
 * 7*tau settling time = 700 ns (0.7 us) << 16.67 us (> 20x safety margin).
 */
#define SEMG_DMA_SAMPLE_FREQ_HZ        (60000)
#define SEMG_OVERSAMPLE_RATIO          (20)
#define SEMG_DECIMATED_RATE_HZ         (1000)

/**
 * @brief DMA Frame Size Calculation:
 * Each conversion result is 2 bytes (SOC_ADC_DIGI_RESULT_BYTES = 2).
 * 1 full 3-channel scan = 3 channels * 2 bytes = 6 bytes.
 * For 20 oversamples: 20 * 6 bytes = 120 bytes per frame (divisible by 4 bytes per conv).
 */
#define SEMG_DMA_FRAME_SIZE            (120)
#define SEMG_DMA_MAX_STORE_BUF_SIZE    (1024)

/* ========================================================================= */
/*                     PREPROCESSING & v3 DSP PARAMETERS                     */
/* ========================================================================= */

/* Validation Thresholds */
#define SEMG_RAIL_MIN_RAW              (15)     /* Rail saturation low (< ~12 mV) */
#define SEMG_RAIL_MAX_RAW              (4080)   /* Rail saturation high (> ~3280 mV) */
#define SEMG_STUCK_WINDOW_LEN          (100)    /* 100 ms sliding window @ 1 kHz */
#define SEMG_STUCK_MAX_RANGE           (2)      /* Range (max - min) <= 2 ADC codes */
#define SEMG_STUCK_MIN_SAME_COUNT      (90)     /* >= 90/100 samples identical code -> stuck */

/* Optional Integer 50 Hz Biquad Notch Filter (0 = off when Bridge filters, 1 = on) */
#ifndef SEMG_ENABLE_NOTCH50
#define SEMG_ENABLE_NOTCH50            (0)
#endif

/* Hardware AFE Parameters for High-Resolution Fixed-Point Scaling */
#define SEMG_AFE_GAIN_X10              (1136)   /* 113.6x nominal gain from MCP6004/LTspice AFE */
#define SEMG_VREF_MV_X100              (165000) /* Virtual Ground Vref = 1650.00 mV (in 0.01 mV) */

/* ========================================================================= */
/*                      UART OUTPUT CONFIGURATION                            */
/* ========================================================================= */

#define SEMG_UART_PORT                 (UART_NUM_0)
#define SEMG_UART_BAUDRATE             (921600) /* 921.6 kbps (~22% load @ 1 kHz v3 CSV) */
#define SEMG_UART_TX_BUF_SIZE          (2048)

/**
 * @brief Output format selector:
 * 1 = Output v3 Referred-To-Input potential in 0.1 uV units (1 unit = 0.1 uV = 1e-7 V)
 * 0 = Output Linearized & Calibrated millivolts at ADC pin (mV)
 */
#define SEMG_UART_OUTPUT_RTI_TENTH_UV  (1)

/* ========================================================================= */
/*                      FREERTOS & MEMORY PARAMS                             */
/* ========================================================================= */

#define SEMG_QUEUE_LENGTH              (128)
#define SEMG_SAMPLER_STACK_SIZE        (4096)
#define SEMG_UART_STACK_SIZE           (4096)

#define SEMG_SAMPLER_PRIORITY          (10)     /* Pinned to Core 0 */
#define SEMG_UART_PRIORITY             (5)      /* Pinned to Core 1 */

#define SEMG_SAMPLER_CORE              (0)
#define SEMG_UART_CORE                 (1)

/* ========================================================================= */
/*                           DATA STRUCTURES                                 */
/* ========================================================================= */

/**
 * @brief Channel Health Status Flags (Bitmask)
 */
typedef enum {
    SEMG_HEALTH_OK                     = 0x00,
    SEMG_HEALTH_RAIL_LOW               = 0x01,  /* Saturated to 0V ground */
    SEMG_HEALTH_RAIL_HIGH              = 0x02,  /* Saturated to 3.3V rail */
    SEMG_HEALTH_STUCK_AT               = 0x04,  /* Disconnected/flatline electrode */
} semg_health_flag_t;

/**
 * @brief Processed 1 kHz sEMG Multi-Channel Frame (v3)
 */
typedef struct {
    uint32_t timestamp_ms;
    int16_t  raw_decimated[SEMG_NUM_CHANNELS];      /* Rounded 12-bit ADC raw (0..4095) */
    int16_t  voltage_mv[SEMG_NUM_CHANNELS];         /* Calibrated & linearized ADC mV */
    int32_t  emg_rti_tenth_uv[SEMG_NUM_CHANNELS];   /* Physical RTI in 0.1 uV (10 = 1.0 uV) */
    uint8_t  health_flags[SEMG_NUM_CHANNELS];       /* Bitmask of semg_health_flag_t */
} semg_processed_frame_t;

#ifdef __cplusplus
}
#endif

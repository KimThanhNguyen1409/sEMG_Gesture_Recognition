/**
 * @file semg_config.h
 * @brief System configuration, pin mapping, DMA & DSP parameters for ESP32-S sEMG system.
 *
 * Target: ESP32-S (ESP32 Classic Xtensa LX6, chip D0WD)
 * AFE: 3-Channel Op-Amp (MCP6004) with Virtual Ground Vref = 1.65V, Gain = 113x
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_adc/adc_continuous.h"
#include "hal/adc_types.h"
#include "hal/uart_types.h"
#include "soc/gpio_num.h"
#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
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

/* ========================================================================= */
/*                   DMA & CONTINUOUS SAMPLING ENGINE                        */
/* ========================================================================= */

/**
 * @brief Continuous ADC DMA Sampling Frequencies:
 * - Aggregate DMA Sample Frequency = 60,000 Hz (60 kHz)
 * - Per-channel sample rate = 60 kHz / 3 channels = 20,000 Hz (20 kHz)
 * - Oversampling Factor = 20
 * - Decimated Output Rate = 20,000 Hz / 20 = 1,000 Hz (1 kHz = 1 ms per frame)
 *
 * Settling & Crosstalk Immunity:
 * Time between channel switching = 1 / 60 kHz = 16.67 us.
 * Hardware anti-kickback filter: R = 100 ohm, C = 1 nF -> tau = 100 ns.
 * 7*tau settling time = 700 ns (0.7 us) << 16.67 us (over 20x safety margin).
 */
#define SEMG_DMA_SAMPLE_FREQ_HZ        (60000)
#define SEMG_OVERSAMPLE_RATIO          (20)
#define SEMG_DECIMATED_RATE_HZ         (1000)

/**
 * @brief DMA Frame Size Calculation:
 * Each conversion result is 2 bytes (SOC_ADC_DIGI_RESULT_BYTES = 2).
 * 1 full 3-channel scan = 3 channels * 2 bytes = 6 bytes.
 * For 20 oversamples: 20 * 6 bytes = 120 bytes per frame (divisible by 4 bytes per conv).
 * 120 bytes at 60 kHz aggregate = exactly 1.0 ms duration.
 */
#define SEMG_DMA_FRAME_SIZE            (120)
#define SEMG_DMA_MAX_STORE_BUF_SIZE    (1024)

/* ========================================================================= */
/*                     PREPROCESSING & DSP PARAMETERS                        */
/* ========================================================================= */

/* Validation Thresholds */
#define SEMG_RAIL_MIN_RAW              (15)     /* Rail saturation low (< ~12 mV) */
#define SEMG_RAIL_MAX_RAW              (4080)   /* Rail saturation high (> ~3280 mV) */
#define SEMG_STUCK_AT_DIFF_THRESH      (2)      /* Max delta to consider sample stuck */
#define SEMG_STUCK_AT_MAX_SAMPLES      (100)    /* 100 ms of stuck samples flags error */

/* Boxcar FIR Moving Average Filter (Power of 2 for fast integer shift) */
#define SEMG_BOXCAR_TAPS               (8)      /* 8-tap Boxcar FIR */
#define SEMG_BOXCAR_SHIFT              (3)      /* 2^3 = 8 (division replaced by >> 3) */

/* Hardware AFE Parameters for Physical Unit Conversion */
#define SEMG_AFE_GAIN                  (113)    /* Hardware gain from KiCad MCP6004 AFE */
#define SEMG_VREF_MV                   (1650)   /* Virtual Ground Vref = 1.65 V */

/* ========================================================================= */
/*                      UART OUTPUT CONFIGURATION                            */
/* ========================================================================= */

#define SEMG_UART_PORT                 (UART_NUM_0)
#define SEMG_UART_BAUDRATE             (115200)
#define SEMG_UART_TX_BUF_SIZE          (1024)

/* Output format selector: 
 * 1 = Output Microvolts RTI (Referred to Input in uV)
 * 0 = Output Calibrated Millivolts (mV at ADC pin)
 */
#define SEMG_UART_OUTPUT_RTI_UV        (1)

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
 * @brief Processed 1 kHz sEMG Multi-Channel Frame
 */
typedef struct {
    uint32_t timestamp_ms;
    int16_t  raw_decimated[SEMG_NUM_CHANNELS];   /* Averaged 12-bit ADC raw (0..4095) */
    int16_t  voltage_mv[SEMG_NUM_CHANNELS];      /* Calibrated & linearized ADC mV */
    int32_t  emg_rti_uv[SEMG_NUM_CHANNELS];      /* Physical Referred-To-Input in uV */
    uint8_t  health_flags[SEMG_NUM_CHANNELS];    /* Bitmask of semg_health_flag_t */
} semg_processed_frame_t;

#ifdef __cplusplus
}
#endif

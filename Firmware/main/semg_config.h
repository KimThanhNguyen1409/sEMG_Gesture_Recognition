/**
 * @file semg_config.h
 * @brief System configuration, pin mapping, DMA, Wi-Fi UDP & v3 DSP parameters for ESP32-S sEMG.
 *
 * Build Modes (Both stream over Wi-Fi UDP + UART, Wi-Fi creds stored dynamically in NVS):
 * - RELEASE  : Full real-time hardware flow (real ADC1 DMA -> Wi-Fi UDP -> Bridge -> AI Server)
 * - SIMULATE : Unseen Test-Split Flash Mock (no circuit needed, BOOT/IO0 switches 4 gestures -> Wi-Fi UDP)
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
#define ESP_OK                    (0)
#define ESP_FAIL                  (-1)
#define GPIO_NUM_36               (36)
#define GPIO_NUM_39               (39)
#define GPIO_NUM_34               (34)
#define GPIO_NUM_35               (35)
#define GPIO_NUM_32               (32)
#define ADC_CHANNEL_0             (0)
#define ADC_CHANNEL_3             (3)
#define ADC_CHANNEL_4             (4)
#define ADC_CHANNEL_6             (6)
#define ADC_CHANNEL_7             (7)
#define ADC_ATTEN_DB_12           (3)
#define SOC_ADC_DIGI_MAX_BITWIDTH (12)
#define UART_NUM_0                (0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*                    BUILD MODE: RELEASE vs SIMULATE                        */
/* ========================================================================= */

/**
 * @brief Select firmware operation mode at build time via build.bat / CMake:
 *   build.bat release   -> -DFIRMWARE_MODE=RELEASE   (default: real ADC + Wi-Fi UDP)
 *   build.bat simulate  -> -DFIRMWARE_MODE=SIMULATE  (unseen Test-Split mock + Wi-Fi UDP)
 */
#if !defined(SEMG_MODE_SIMULATE) && !defined(SEMG_MODE_RELEASE)
#define SEMG_MODE_RELEASE              (1)
#endif

/**
 * @brief Hardware board profile for RELEASE mode:
 *   SEMG_HW_BOARD_PROTO_5V (1): Breadboard 5V AFE + 10k/20k Divider on GPIO 34/35/32 + Auto-Tare
 *   SEMG_HW_BOARD_PCB_3V3  (2): Fabricated 3.3V PCB on GPIO 36/39/34 (Vref=1.65V)
 */
#define SEMG_HW_BOARD_PROTO_5V         (1)
#define SEMG_HW_BOARD_PCB_3V3          (2)
#ifndef SEMG_HW_BOARD
#define SEMG_HW_BOARD                  (SEMG_HW_BOARD_PROTO_5V)
#endif

#if defined(SEMG_MODE_SIMULATE)
#define SEMG_MODE_STR                  "SIMULATE (Mock Test-Split + Wi-Fi UDP)"
#define SEMG_USE_DIVIDER_AUTOTARE      (0)
#elif (SEMG_HW_BOARD == SEMG_HW_BOARD_PROTO_5V)
#define SEMG_MODE_STR                  "RELEASE (5V Proto + Divider GPIO 34/35/32 + Wi-Fi UDP)"
#define SEMG_USE_DIVIDER_AUTOTARE      (1)
#else
#define SEMG_MODE_STR                  "RELEASE (3.3V PCB GPIO 36/39/34 + Wi-Fi UDP)"
#define SEMG_USE_DIVIDER_AUTOTARE      (0)
#endif

/* ========================================================================= */
/*                              HARDWARE PINS                                */
/* ========================================================================= */

#define SEMG_NUM_CHANNELS              (3)

#if defined(SEMG_MODE_RELEASE) && (SEMG_HW_BOARD == SEMG_HW_BOARD_PROTO_5V)
/* 38-pin ESP-32S DevKit header pins GPIO 34, 35, 32 (ADC1 CH6, 7, 4) */
#define SEMG_CH1_GPIO                  (GPIO_NUM_34)
#define SEMG_CH1_ADC_CHANNEL           (ADC_CHANNEL_6) /* Pin 10: GPIO34 (ADC1_6) -> CH1 */

#define SEMG_CH2_GPIO                  (GPIO_NUM_35)
#define SEMG_CH2_ADC_CHANNEL           (ADC_CHANNEL_7) /* Pin 11: GPIO35 (ADC1_7) -> CH2 */

#define SEMG_CH3_GPIO                  (GPIO_NUM_32)
#define SEMG_CH3_ADC_CHANNEL           (ADC_CHANNEL_4) /* Pin 12: GPIO32 (ADC1_4) -> CH3 */
#else
/* Custom PCB layout / Mock channels: Pads 4, 5, 6 -> GPIO 36, 39, 34 */
#define SEMG_CH1_GPIO                  (GPIO_NUM_36)
#define SEMG_CH1_ADC_CHANNEL           (ADC_CHANNEL_0) /* SENSOR_VP (Pad 4) -> ADC_CH1 */

#define SEMG_CH2_GPIO                  (GPIO_NUM_39)
#define SEMG_CH2_ADC_CHANNEL           (ADC_CHANNEL_3) /* SENSOR_VN (Pad 5) -> ADC_CH2 */

#define SEMG_CH3_GPIO                  (GPIO_NUM_34)
#define SEMG_CH3_ADC_CHANNEL           (ADC_CHANNEL_6) /* IO34      (Pad 6) -> ADC_CH3 */
#endif

#define SEMG_ADC_ATTEN                 (ADC_ATTEN_DB_12)
#define SEMG_ADC_BITWIDTH              (SOC_ADC_DIGI_MAX_BITWIDTH) /* 12-bit */

/* Interactive mode button (IO0 / BOOT button on ESP32-S) */
#define SEMG_DEMO_BTN_GPIO             (0)

/* ========================================================================= */
/*                   DMA & CONTINUOUS SAMPLING ENGINE                        */
/* ========================================================================= */

#define SEMG_DMA_SAMPLE_FREQ_HZ        (60000)
#define SEMG_OVERSAMPLE_RATIO          (20)
#define SEMG_DECIMATED_RATE_HZ         (1000)

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
#define SEMG_AFE_GAIN_X10              (1136)   /* 113.6x nominal gain from Op-Amp AFE */
#define SEMG_VREF_MV_X100              (165000) /* Nominal Vref = 1650.00 mV (in 0.01 mV) */

/**
 * @brief SIMULATE Mode 5V -> 3.3V Voltage Divider Parameters:
 *   Op-Amp Out (0..5V, Vref=2.5V) --[ R_TOP ]--+--> ESP32 GPIO (0..3.33V, Vref=1.67V)
 *                                              |
 *                                           [ R_BOT ]
 *                                              |
 *                                             GND
 * Default: R_TOP = 10 kOhm, R_BOT = 20 kOhm (attenuation 2/3, inverse recovery 1.5x).
 * If using different resistors (e.g. 10k & 18k), update the two values below.
 */
#define SEMG_SIM_R_TOP_OHM             (10000LL)
#define SEMG_SIM_R_BOT_OHM             (20000LL)
#define SEMG_SIM_STARTUP_TARE_SAMPLES  (500u)   /* Auto-zero DC over first 500 ms */

/* ========================================================================= */
/*                      UART OUTPUT CONFIGURATION                            */
/* ========================================================================= */

#define SEMG_UART_PORT                 (UART_NUM_0)
#define SEMG_UART_BAUDRATE             (921600) /* 921.6 kbps (~22% load @ 1 kHz v3 CSV) */
#define SEMG_UART_TX_BUF_SIZE          (2048)

/* Set to 1 only when debugging throughput in idf.py monitor (keep 0 for clean CSV stream) */
#ifndef SEMG_ENABLE_PERIODIC_LOG
#define SEMG_ENABLE_PERIODIC_LOG       (0)
#endif

/**
 * @brief Default output format for RELEASE / TESTING:
 * 1 = Output v3 Referred-To-Input potential in 0.1 uV units (1 unit = 0.1 uV = 1e-7 V)
 * 0 = Output Linearized & Calibrated millivolts at ADC pin (mV)
 * (In SIMULATE mode, starts at 0 [CHECK_RAW_MV] and toggles live via IO0/BOOT button!)
 */
#define SEMG_UART_OUTPUT_RTI_TENTH_UV  (1)

/* ========================================================================= */
/*                WIFI UDP RAW STREAM (ALL MODES + NVS CONFIG)               */
/* ========================================================================= */

#ifndef SEMG_ENABLE_WIFI_UDP
#define SEMG_ENABLE_WIFI_UDP           (1)
#endif

/* Factory defaults (overridden dynamically at runtime via NVS / PC Launcher App) */
#define SEMG_WIFI_DEFAULT_SSID         "Nha Tre 2.4G"
#define SEMG_WIFI_DEFAULT_PASS         "01684306403"
#define SEMG_UDP_DEFAULT_DEST_IP       "192.168.1.13"
#define SEMG_UDP_DEFAULT_DEST_PORT     (5005)
#define SEMG_UDP_BATCH_SAMPLES         (25u)    /* 25 ms/packet @ 1 kHz = 40 pkt/s */
#define SEMG_UDP_TX_BUF_SIZE           (1024u)  /* < 1472 B MTU, zero fragmentation */

/* ========================================================================= */
/*                      FREERTOS & MEMORY PARAMS                             */
/* ========================================================================= */

#define SEMG_QUEUE_LENGTH              (128)
#define SEMG_SAMPLER_STACK_SIZE        (4096)
#if (SEMG_ENABLE_WIFI_UDP == 1)
#define SEMG_UART_STACK_SIZE           (6144)   /* Extra headroom for lwIP sendto() */
#else
#define SEMG_UART_STACK_SIZE           (4096)
#endif

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
    int16_t  voltage_mv[SEMG_NUM_CHANNELS];         /* Calibrated & linearized ADC pin mV */
    int32_t  emg_rti_tenth_uv[SEMG_NUM_CHANNELS];   /* Physical RTI in 0.1 uV (10 = 1.0 uV) */
    uint8_t  health_flags[SEMG_NUM_CHANNELS];       /* Bitmask of semg_health_flag_t */
    uint8_t  output_rti_mode;                       /* 0 = voltage_mv CSV, 1 = 0.1 uV RTI CSV */
} semg_processed_frame_t;

#ifdef __cplusplus
}
#endif

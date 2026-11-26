# ============================================================================
# Firmware Build Mode Selection: RELEASE (default) vs TESTING (Mock ADC)
# Usage:
#   idf.py -DFIRMWARE_MODE=RELEASE build   (Real PCB ADC1 DMA)
#   idf.py -DFIRMWARE_MODE=TESTING build   (Unseen Test-Split Mock + BOOT btn)
# ============================================================================
if(NOT DEFINED FIRMWARE_MODE)
    set(FIRMWARE_MODE "RELEASE")
endif()

string(TOUPPER "${FIRMWARE_MODE}" FIRMWARE_MODE_UPPER)

if(FIRMWARE_MODE_UPPER STREQUAL "TESTING")
    message(STATUS "[SEMG] Build Mode: TESTING (using semg_adc_mock.c)")
    set(ADC_SRC "semg_adc_mock.c")
    set(MODE_DEF "SEMG_MODE_TESTING=1")
else()
    message(STATUS "[SEMG] Build Mode: RELEASE (using real hardware semg_adc.c)")
    set(ADC_SRC "semg_adc.c")
    set(MODE_DEF "SEMG_MODE_RELEASE=1")
endif()

idf_component_register(
    SRCS 
        "main.c"
        "${ADC_SRC}"
        "semg_pipeline.c"
        "semg_uart.c"
    INCLUDE_DIRS 
        "."
    PRIV_REQUIRES 
        esp_adc 
        esp_driver_uart 
        esp_driver_gpio
        esp_timer
        driver 
        freertos
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ${MODE_DEF})

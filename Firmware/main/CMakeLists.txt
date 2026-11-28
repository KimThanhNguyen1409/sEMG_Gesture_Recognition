# ============================================================================
# Firmware Build Mode Selection (Both stream over Wi-Fi UDP + NVS config):
#   idf.py -DFIRMWARE_MODE=RELEASE  build  (Real ADC DMA full flow)
#   idf.py -DFIRMWARE_MODE=SIMULATE build  (Unseen Test-Split Mock + BOOT btn)
# ============================================================================
if(NOT DEFINED FIRMWARE_MODE)
    set(FIRMWARE_MODE "RELEASE")
endif()

string(TOUPPER "${FIRMWARE_MODE}" FIRMWARE_MODE_UPPER)

if(FIRMWARE_MODE_UPPER STREQUAL "SIMULATE")
    message(STATUS "[SEMG] Build Mode: SIMULATE (Unseen Test-Split Mock + Wi-Fi UDP)")
    set(ADC_SRC "semg_adc_mock.c")
    set(MODE_DEF "SEMG_MODE_SIMULATE=1")
else()
    message(STATUS "[SEMG] Build Mode: RELEASE (Real ADC DMA + Wi-Fi UDP)")
    set(ADC_SRC "semg_adc.c")
    set(MODE_DEF "SEMG_MODE_RELEASE=1")
endif()

idf_component_register(
    SRCS 
        "main.c"
        "${ADC_SRC}"
        "semg_pipeline.c"
        "semg_uart.c"
        "semg_wifi_udp.c"
    INCLUDE_DIRS 
        "."
    PRIV_REQUIRES 
        esp_adc 
        esp_driver_uart 
        esp_driver_gpio
        esp_timer
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        driver 
        freertos
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ${MODE_DEF})

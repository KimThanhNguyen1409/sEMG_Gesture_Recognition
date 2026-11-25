# Support optional mock ADC mode: idf.py -DSEMG_MOCK_ADC=1 build
if(SEMG_MOCK_ADC)
    set(ADC_SRC "semg_adc_mock.c")
else()
    set(ADC_SRC "semg_adc.c")
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

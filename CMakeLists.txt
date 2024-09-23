idf_component_register(
    SRCS
        "rmt_pulse.c"
        "ed047tc1.c"
        "lilygo-ttgo-t5-47.c"
        "epd_driver.c"
        "i2s_data_bus.c"
        "font.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver spiffs
    PRIV_REQUIRES fatfs esp_lcd
)


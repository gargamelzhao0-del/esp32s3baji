idf_component_register(
    SRCS "baji_display.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES baji_board_profile esp_driver_gpio esp_driver_spi esp_hw_support freertos
)

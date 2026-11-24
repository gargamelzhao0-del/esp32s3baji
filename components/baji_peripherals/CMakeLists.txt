idf_component_register(
    SRCS "baji_peripherals.c" "cst9217.c" "axp2101.c" "es8311.c" "baji_speaker.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES baji_board_profile esp_driver_gpio esp_driver_i2c esp_driver_i2s freertos heap
)

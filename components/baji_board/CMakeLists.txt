idf_component_register(SRCS "baji_board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES baji_peripherals esp_common
                       PRIV_REQUIRES baji_display esp_psram spi_flash heap freertos)

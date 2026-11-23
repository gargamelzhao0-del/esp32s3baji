idf_component_register(SRCS "baji_vsync.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES baji_board_profile esp_driver_gpio esp_timer freertos)

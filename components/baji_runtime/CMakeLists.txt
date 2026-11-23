idf_component_register(SRCS "baji_runtime.c" INCLUDE_DIRS "include"
                       REQUIRES baji_board freertos PRIV_REQUIRES baji_vsync esp_timer)

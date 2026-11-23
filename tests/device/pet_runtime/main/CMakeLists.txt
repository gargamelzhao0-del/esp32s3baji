idf_component_register(SRCS "bench.c" "schedule_check.c"
                       PRIV_REQUIRES baji_pet_app baji_runtime baji_vsync
                                     esp_driver_usb_serial_jtag esp_rom esp_timer heap)
set(BAJI_SCHEDULE_CHECK_MS 3300 CACHE STRING "Idle duration for the on-device scheduler check")
target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_SCHEDULE_CHECK_MS=${BAJI_SCHEDULE_CHECK_MS})

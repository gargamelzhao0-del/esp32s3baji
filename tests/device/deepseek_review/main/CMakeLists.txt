idf_component_register(SRCS "qualify.c" "hd_check.c" "breath_trace.c" "body_probe.c" "live_probe.c"
                       PRIV_INCLUDE_DIRS "../../../../components/baji_deepseek"
                       PRIV_REQUIRES baji_deepseek baji_deepseek_hd baji_runtime baji_scene_damage baji_vsync esp_driver_usb_serial_jtag esp_rom esp_timer heap)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=baji_board_paint_round_regions")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=dsk_advance")
set(DSK_LIVE_CHECK_MS 60000 CACHE STRING "Observe live application submissions for this many milliseconds")
target_compile_definitions(${COMPONENT_LIB} PRIVATE DSK_LIVE_CHECK_MS=${DSK_LIVE_CHECK_MS})

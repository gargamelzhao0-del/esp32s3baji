get_filename_component(app "${CMAKE_CURRENT_LIST_DIR}/../../../../main" ABSOLUTE)
idf_component_register(SRCS "bench.c" "${app}/pet_player.c" "${app}/pet_app.c"
                       INCLUDE_DIRS "${app}"
                       PRIV_REQUIRES baji_board baji_pet baji_scene_damage baji_vsync
                                     esp_driver_usb_serial_jtag esp_rom esp_timer heap)

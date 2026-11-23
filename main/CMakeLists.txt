idf_component_register(SRCS "app_main.c" "board_check.c" "pet_app.c" "pet_player.c"
                       PRIV_REQUIRES baji_board baji_pet baji_scene_damage baji_vsync esp_timer
                       INCLUDE_DIRS "")

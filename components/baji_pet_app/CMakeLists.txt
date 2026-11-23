idf_component_register(SRCS "pet_app.c" "pet_player.c" INCLUDE_DIRS "include"
                       REQUIRES baji_runtime baji_pet baji_scene_damage PRIV_REQUIRES esp_timer)

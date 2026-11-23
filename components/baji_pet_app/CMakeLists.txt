idf_component_register(SRCS "pet_app.c" "pet_player.c" INCLUDE_DIRS "include"
                       REQUIRES baji_runtime baji_pet baji_scene_damage baji_agent_port PRIV_REQUIRES baji_agent_ui esp_timer)

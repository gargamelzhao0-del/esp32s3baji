set(app_sources "pet_app.c" "pet_player.c")
set(app_deps baji_agent_ui esp_timer)
set(app_assets)
if(NOT BAJI_PET_EXTERNAL_ASSET)
    list(APPEND app_sources "pet_companion.c" "pet_interactions.c" "pet_menu.c" "pet_effects.c" "pet_feedback.c")
    list(APPEND app_deps baji_pet_clips baji_peripherals nvs_flash)
    set(app_assets EMBED_FILES "assets/pet_tones.pcm")
endif()
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include"
                       REQUIRES baji_runtime baji_pet baji_scene_damage baji_agent_port
                       PRIV_REQUIRES ${app_deps} ${app_assets})
if(NOT BAJI_PET_EXTERNAL_ASSET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_PET_COMPANION=1)
endif()

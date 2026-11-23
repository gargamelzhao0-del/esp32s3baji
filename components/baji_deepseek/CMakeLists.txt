set(embed)
if(NOT BAJI_DEEPSEEK_EXTERNAL_ASSET)
    set(embed EMBED_FILES "assets/scene.dsk")
endif()
idf_component_register(SRCS "deepseek_review.c" "deepseek_canvas.c" "deepseek_app.c" "deepseek_controls.c"
                       INCLUDE_DIRS "include" REQUIRES baji_graphics baji_board baji_runtime baji_agent_port
                       PRIV_REQUIRES baji_agent_ui baji_deepseek_hd baji_scene_damage esp_timer esp_rom ${embed})
if(BAJI_DEEPSEEK_EXTERNAL_ASSET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_DEEPSEEK_EXTERNAL_ASSET=1)
endif()

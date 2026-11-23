set(embed)
if(NOT BAJI_DEEPSEEK_EXTERNAL_ASSET)
    set(embed EMBED_FILES "assets/scene.hds")
endif()
idf_component_register(SRCS "deepseek_hd.c" INCLUDE_DIRS "include"
                       REQUIRES baji_graphics PRIV_REQUIRES esp_rom heap esp_partition ${embed})
if(BAJI_DEEPSEEK_EXTERNAL_ASSET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_DEEPSEEK_EXTERNAL_ASSET=1)
endif()

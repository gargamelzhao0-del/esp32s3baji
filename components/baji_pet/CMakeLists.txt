set(embed)
if(NOT BAJI_PET_EXTERNAL_ASSET)
    set(embed EMBED_FILES "assets/motion.bpz")
endif()
idf_component_register(SRCS "baji_pet.c" "baji_pet_asset.c" "baji_pet_motion.c" INCLUDE_DIRS "include" REQUIRES baji_graphics esp_common
                       PRIV_REQUIRES esp_rom heap ${embed})
if(BAJI_PET_EXTERNAL_ASSET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_PET_EXTERNAL_ASSET=1)
endif()

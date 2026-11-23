get_filename_component(badge_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(SRCS "replay.c" PRIV_REQUIRES baji_board esp_timer esp_driver_gpio esp_rom)
idf_build_get_property(python PYTHON)
set(assets "${badge_root}/design/pixel-badge/assets")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/pet_replay_asset.h")
add_custom_command(OUTPUT "${generated}"
    COMMAND "${python}" "${CMAKE_CURRENT_LIST_DIR}/../prepare.py" "${assets}" "${generated}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../prepare.py" "${assets}/heartHug.png" "${assets}/sneeze.png"
    VERBATIM)
add_custom_target(pet_replay_atlas DEPENDS "${generated}")
add_dependencies(${COMPONENT_LIB} pet_replay_atlas)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

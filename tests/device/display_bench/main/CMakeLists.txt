get_filename_component(badge_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(SRCS "bench.c" PRIV_INCLUDE_DIRS "${badge_root}/main"
    PRIV_REQUIRES baji_board baji_display esp_hw_support esp_timer esp_driver_gpio)
# Keep this diagnostic buildable against revisions before circular clipping.
set(board_api "${badge_root}/components/baji_board/include/baji_board.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${board_api}")
file(STRINGS "${board_api}" round_api REGEX "esp_err_t baji_board_paint_round[(]")
if(round_api)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_BENCH_ROUND=1)
endif()
file(STRINGS "${board_api}" batch_api REGEX "esp_err_t baji_board_paint_round_regions[(]")
if(batch_api)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BAJI_BENCH_BATCH=1)
endif()

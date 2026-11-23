idf_component_register(SRCS "baji_ble.c" "ble_fragments.c"
                       INCLUDE_DIRS "include"
                       REQUIRES baji_link PRIV_REQUIRES bt)

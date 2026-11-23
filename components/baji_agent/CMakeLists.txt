idf_component_register(SRCS "agent_sync.c" "agent_wire.c" INCLUDE_DIRS "include"
                       REQUIRES baji_link baji_agent_port PRIV_REQUIRES esp_timer esp_hw_support mbedtls)

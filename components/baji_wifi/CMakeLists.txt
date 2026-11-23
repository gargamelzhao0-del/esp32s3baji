idf_component_register(SRCS "baji_wifi.c" "wifi_http.c" INCLUDE_DIRS "include"
                       REQUIRES baji_link
                       PRIV_REQUIRES esp_wifi esp_event esp_netif esp_http_server esp_timer freertos)

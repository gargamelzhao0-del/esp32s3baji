idf_component_register(SRCS "connectivity.c" "settings.c" "usb_setup.c" INCLUDE_DIRS "include"
    REQUIRES baji_agent_port esp_common PRIV_REQUIRES baji_agent baji_wifi baji_ble esp_netif
    esp_event esp_driver_usb_serial_jtag nvs_flash esp_hw_support bootloader_support)

idf_component_register(SRCS "WIFI_Manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES     esp_wifi esp_common nvs_flash esp_event esp_netif)

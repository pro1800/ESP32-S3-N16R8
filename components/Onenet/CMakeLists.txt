idf_component_register(SRCS "onenet_ota.c" "onenet_token.c" "ontnet_mqtt.c" "onenet_dm.c"
                       INCLUDE_DIRS "."
                       REQUIRES mbedtls mqtt json esp_driver_gpio esp_driver_ledc My_WS2812 app_update esp_http_client)

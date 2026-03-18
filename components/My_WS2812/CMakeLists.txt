idf_component_register(SRCS "My_WS2812.c"
                       INCLUDE_DIRS "." 
                       REQUIRES     esp_driver_gpio espressif__led_strip)

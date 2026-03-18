# 声明源码文件：main.c（你的RGB灯驱动代码）
# INCLUDE_DIRS "."：包含当前目录的头文件
# REQUIRES：显式依赖led_strip、rmt、driver组件（ESP-IDF自动加载）
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "." "../components/My_WS2812" "../components/WIFI_Manager" "../components/button" "../components/Onenet"
                    REQUIRES spiffs driver led_strip WIFI_Manager ws_server ap_wifi button)

spiffs_create_partition_image(html ../components/html FLASH_IN_PROJECT)

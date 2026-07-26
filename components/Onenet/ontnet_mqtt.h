#ifndef _ONTNET_MQTT_H_
#define _ONTNET_MQTT_H_
#include "esp_err.h"

//产品ID
#define ONENET_PRODUCT_ID   "f98yB7uWCO"
//产品密钥
#define ONEENT_PRODUCT_ACCESS_KEY   "aamJmZUff9vdsxUp4dMnPauRc9OJr/nFswFfw0uR8dk="
//设备名称
#define ONENET_DEVICE_NAME  "esp32s3n16r801"

esp_err_t onenet_start(void);

esp_err_t onenet_subscribe(void);

esp_err_t onenet_post_property_data(const char* data);

void onenet_property_ack(const char* id,int code,const char* message);
void onenet_ota_ack(const char* id,int code,const char* message);

#endif
#ifndef _ONENET_OTA_H_
#define  _ONENET_OTA_H_
#include <stdio.h>

const char* ger_app_version(void);
void set_app_valid(int valid);
esp_err_t onenet_ota_upload_version(void);
esp_err_t onenet_ota_upload_status(int tid, int progress, int step);
void onenet_ota_ack(const char* id, int code, const char* message);
void onenet_ota_start(void);
esp_err_t onenet_ota_download(int tid);
esp_err_t  onenet_ota_check_task(const char* type,const char* version);


#endif

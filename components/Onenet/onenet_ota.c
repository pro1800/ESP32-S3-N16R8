#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "stdio.h"
#include "onenet_ota.h"
#include "ontnet_mqtt.h"
#include "onenet_token.h"
#include "cJSON.h"
#include "esp_log.h"

#define     MAX_DATA_BUFF   1024
//ota基础url
#define     ONENET_OTA_URL  "http://iot-api.heclouds.com/fuse-ota"
//#define     ONENET_OTA_URL  "https://iot-api.heclouds.com/thingmodel/"

//token合法时间戳
#define     TOKEN_TIMESTAMP     1924833600
//接收到的http 数据
static uint8_t data_buff[MAX_DATA_BUFF];
//接收到的http数据长度
static size_t   data_buff_len = 0;
//log tag
static const char* TAG = "onenet_ota";
//ota任务相关变量
static char target_version[64] = {0};
static int task_id = 0;
static bool ota_is_running = false;

//OTA 分区直写相关变量
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *update_partition = NULL;
static bool is_downloading_firmware = false;
static bool ota_write_success = false;  // OTA 写入成功标志

const char* ger_app_version(void){
    static char app_version[32] = {0};
    if(app_version[0] == 0){
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_app_desc_t app_desc;
        esp_ota_get_partition_description(running,&app_desc);
        snprintf(app_version,sizeof(app_version),"%s",app_desc.version);
    }
    return app_version;
}

void set_app_valid(int valid){
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if(esp_ota_get_state_partition(running, &state) == ESP_OK)
    {
        if(state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            if(valid)
            {
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else
            {
                esp_ota_mark_app_invalid_rollback();
            }
        }
    }
}

/**
 * http事件回调函数
 * @param evt 包含http的数据
 * @return 错误码
 */
static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:    //错误事件
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            if(is_downloading_firmware && ota_handle != 0)
            {
                esp_ota_abort(ota_handle);
                ota_handle = 0;
            }
            break;
        case HTTP_EVENT_ON_CONNECTED:    //连接成功事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:    //发送头事件
            //ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:    //接收头事件
            {
                // 如果是固件下载，初始化 OTA 操作
                if(is_downloading_firmware)
                {
                    update_partition = esp_ota_get_next_update_partition(NULL);
                    if(update_partition == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to get update partition");
                        break;
                    }
                    
                    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
                    if(err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_begin failed, err=0x%x", err);
                        ota_handle = 0;
                        break;
                    }
                    ESP_LOGI(TAG, "OTA started, writing to partition: %s", update_partition->label);
                }
            }
            break;
        case HTTP_EVENT_ON_DATA:    //接收数据事件
            {
                if(is_downloading_firmware && ota_handle != 0)
                {
                    // 直接写入 OTA 分区
                    esp_err_t err = esp_ota_write(ota_handle, evt->data, evt->data_len);
                    if(err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_write failed, err=0x%x", err);
                        esp_ota_abort(ota_handle);
                        ota_handle = 0;
                        break;
                    }
                    ESP_LOGI(TAG, "Written %d bytes to OTA partition", evt->data_len);
                }
                else
                {
                    // 非固件下载，存到缓冲区（用于查询/上报等 JSON 响应）
                    size_t copy_len = 0;
                    ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
                    printf("HTTP_EVENT_ON_DATA data=%.*s\r\n", evt->data_len,(char*)evt->data);
                    if(evt->data_len > MAX_DATA_BUFF - data_buff_len)
                    {
                        copy_len = MAX_DATA_BUFF - data_buff_len;
                    }
                    else
                    {
                        copy_len = evt->data_len;
                    }
                    //将数据存到data_buff里面
                    memcpy(&data_buff[data_buff_len],evt->data,copy_len);
                    data_buff_len += copy_len;
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:    //会话完成事件
            {
                if(is_downloading_firmware && ota_handle != 0)
                {
                    // 完成 OTA 写入
                    esp_err_t err = esp_ota_end(ota_handle);
                    if(err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_end failed, err=0x%x", err);
                        ota_handle = 0;
                        break;
                    }
                    
                    // 设置启动分区
                    err = esp_ota_set_boot_partition(update_partition);
                    if(err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed, err=0x%x", err);
                        break;
                    }
                    
                    ESP_LOGI(TAG, "OTA write complete. Boot partition has been set to: %s", update_partition->label);
                    ota_write_success = true;  // 标记 OTA 写入成功
                    ota_handle = 0;
                }
                data_buff_len = 0;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:    //断开事件
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            if(is_downloading_firmware && ota_handle != 0)
            {
                esp_ota_abort(ota_handle);
                ota_handle = 0;
            }
            data_buff_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            //ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}
/**
 * 发起http请求
 * @param url 请求地址
 * @param method 请求方法
 * @param payload 消息体内容
 * @return 错误码
 */
static esp_err_t onenet_ota_http_connect(const char* url,esp_http_client_method_t method,char* post_data)
{
    esp_http_client_config_t config =
    {
        .url = url,
        .event_handler = http_client_event_handler,
    };
    //初始化结构体
    esp_http_client_handle_t http_client = esp_http_client_init(&config);   //初始化http连接
    if(!http_client)
    {
        ESP_LOGI(TAG,"http_client init fail!");
        return ESP_FAIL;
    }

    char* token = (char*)malloc(256);
    memset(token,0,256);
    //计算token
    dev_token_generate(token,SIG_METHOD_SHA256,TOKEN_TIMESTAMP,ONENET_PRODUCT_ID,NULL,ONEENT_PRODUCT_ACCESS_KEY);
    ESP_LOGI(TAG,"user token:%s",token);
    //设置发送请求头
    esp_http_client_set_method(http_client, method);
    esp_http_client_set_header(http_client,"Content-Type","application/json");
    esp_http_client_set_header(http_client,"Authorization",token);
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");
    if(post_data)
    {
        ESP_LOGI(TAG,"post data:%s",post_data);
        esp_http_client_set_post_field(http_client,post_data,strlen(post_data));
    }
    data_buff_len = 0;
    memset(data_buff,0,sizeof(data_buff));
    //esp_http_client_perform这句函数会阻塞，直到完整的http请求结束才返回
    esp_err_t err  = esp_http_client_perform(http_client);
    free(token);
    //清理操作
    esp_http_client_cleanup(http_client);
    return err;
}

/**
 * 上报版本号
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_ota_upload_version(void)
{
    //格式：{"s_version":"V1.3", "f_version": "V2.0"}
    char version_info[128];
    char url[256];
    esp_err_t ret = ESP_FAIL;
    //获取版本号
    const char* version = ger_app_version();
    //生成消息体内容（版本号）
    snprintf(version_info,sizeof(version_info),"{\"s_version\":\"%s\", \"f_version\": \"%s\"}",version,version);
    //计算url
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/version",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_POST,version_info))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG,"Upload version fail!");
        return ret;
    }
    return ret;
}

/**
 * 查询升级任务状态
 * @param type = 1,说明是完整包，type=2,说明是差分包
 * @param version 当前设备版本
 * @return 错误码
 */
esp_err_t  onenet_ota_check_task(const char* type,const char* version)
{
    char url[256];
    esp_err_t ret = ESP_FAIL;
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/check?type=%s&version=%s",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,type,version);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_GET,NULL))
    {
        cJSON *root =  cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");    //错误代码
            int code_value = code_js ? (int)cJSON_GetNumberValue(code_js) : -1;
            
            if(code_value == 0)
            {
                // 有新版本可升级
                cJSON *data_js = cJSON_GetObjectItem(root,"data");
                cJSON* target_js = cJSON_GetObjectItem(data_js,"target");
                cJSON* tid_js = cJSON_GetObjectItem(data_js,"tid");
                if(target_js && tid_js)    //我们感兴趣的只有任务id和目标版本号
                {
                    snprintf(target_version,sizeof(target_version),"%s",cJSON_GetStringValue(target_js));
                    task_id = cJSON_GetNumberValue(tid_js);    //取出任务id
                    ret = ESP_OK;
                }
            }
            else if(code_value == 12012)
            {
                // 没有新版本可升级，这是正常的情况
                ESP_LOGI(TAG, "No new firmware version available");
                task_id = 0;  // 设置为0表示没有新版本
                ret = ESP_OK;
            }
            else
            {
                // 真正的错误
                ESP_LOGW(TAG, "Check OTA task returned code: %d", code_value);
                ret = ESP_FAIL;
            }
            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGI(TAG,"Check ota task fail!");
            return ret;
        }
    }
    else
    {
        return ret;
    }
    return ret;
}

/**
 * 准确启动ota下载前的回调函数，在这里可以设置请求头
 * @param http_client http客户端句柄
 * @return 错误码
 */
static esp_err_t http_ota_init_callback(esp_http_client_handle_t http_client)
{
    static char token[256];
    memset(token,0,256);
    dev_token_generate(token,SIG_METHOD_SHA256,TOKEN_TIMESTAMP,ONENET_PRODUCT_ID,NULL,ONEENT_PRODUCT_ACCESS_KEY);
    ESP_LOGI(TAG,"user token:%s",token);
    //设置发送请求 
    esp_http_client_set_method(http_client, HTTP_METHOD_GET);
    esp_http_client_set_header(http_client,"Content-Type","application/json");
    esp_http_client_set_header(http_client,"Authorization",token);
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");
    return ESP_OK;
}

/**
 * 启动ota下载
 * @param tid 升级任务id，通过查询升级任务可获取
 * @return 错误码
 */
esp_err_t onenet_ota_download(int tid)
{
    esp_err_t ota_finish_err = ESP_OK;
    char url[256];
    snprintf(url,sizeof(url),ONENET_OTA_URL"/%s/%s/%d/download",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,tid);
    
    // 标记正在下载固件
    is_downloading_firmware = true;
    ota_write_success = false;  // 重置 OTA 写入成功标志
    ota_handle = 0;
    update_partition = NULL;
    
    // 使用 http_client 下载固件（直接写入分区）
    esp_http_client_config_t config =
    {
        .url = url,
        .event_handler = http_client_event_handler,
        .timeout_ms = 30000,  // 30 秒超时
        .buffer_size = 4096,  // 增大缓冲区
    };
    
    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if(!http_client)
    {
        ESP_LOGE(TAG,"http_client init fail!");
        is_downloading_firmware = false;
        return ESP_FAIL;
    }
    
    // 设置请求头
    http_ota_init_callback(http_client);
    
    // 执行 HTTP 请求
    ota_finish_err = esp_http_client_perform(http_client);
    esp_http_client_cleanup(http_client);
    
    is_downloading_firmware = false;
    
    if(ota_finish_err == ESP_OK)
    {
        if(ota_write_success)
        {
            ESP_LOGI(TAG, "OTA download and write successful. Partition set, ready to reboot...");
            return ESP_OK;
        }
        else
        {
            ESP_LOGE(TAG, "OTA write was not properly completed");
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "OTA download failed 0x%x", ota_finish_err);
        if(ota_handle != 0)
        {
            esp_ota_abort(ota_handle);
            ota_handle = 0;
        }
    }
    return ota_finish_err;
}

static void onenet_ota_task(void *param)
{
    esp_err_t ret;
    //上报当前版本号
    ret = onenet_ota_upload_version();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"Upload version failed!");
        goto delete_ota_task;
    }
    //检测升级任务
    ret = onenet_ota_check_task("1",ger_app_version());
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"Check OTA task failed!");
        goto delete_ota_task;
    }
    
    // 检查是否有新版本（task_id=0表示没有新版本）
    if(task_id == 0)
    {
        ESP_LOGI(TAG, "Device is already running the latest firmware version");
        goto delete_ota_task;
    }
    
    //上报任务升级状态 (step=2表示下载中)
    ret = onenet_ota_upload_status(task_id,10,2);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"upload status failed!");
        goto delete_ota_task;
    }
    //进行http下载
    ret = onenet_ota_download(task_id);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"OTA download failed!");
        goto delete_ota_task;
    }
    //上报任务升级状态 (step=3表示下载完成)
    ret = onenet_ota_upload_status(task_id,100,3);
    //重启
    esp_restart();
delete_ota_task:
    ota_is_running = false;
    vTaskDelete(NULL);
}

/**
 * 上报OTA任务升级状态
 * @param tid 任务id
 * @param progress 进度(0-100)
 * @param step 步骤(1=准备下载, 2=下载中, 3=下载完成)
 * @return 错误码
 */
esp_err_t onenet_ota_upload_status(int tid, int progress, int step)
{
    char url[256];
    char status_data[128];
    esp_err_t ret = ESP_FAIL;
    
    //格式：{"step":2,"progress":10}
    snprintf(status_data,sizeof(status_data),"{\"step\":%d,\"progress\":%d}",step,progress);
    //计算url - 使用 /status 端点而非 /progress
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/%d/status",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,tid);
    
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_POST,status_data))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG,"Upload status fail!");
        return ret;
    }
    return ret;
}

/**
 * 启动onenet ota升级流程
 * @param 无
 * @return 无
 */
void onenet_ota_start(void)
{
    if(ota_is_running)
        return;
    ota_is_running = true;
    ESP_LOGI(TAG,"Start OTA");
    xTaskCreatePinnedToCore(onenet_ota_task,"onenet_ota",8192,NULL,2,NULL,1);
}


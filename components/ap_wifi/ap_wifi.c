#include "ap_wifi.h"
#include "string.h"
#include "esp_log.h"
#include "sys/stat.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "WIFI_Manager.h"
#include "esp_err.h"
#include "ws_server.h"

//日志标签
#define TAG "ap_wifi"

//事件标志位
#define APCFG_BIT (1 << 0)

//html网页在spiffs文件系统中的路径
#define INDEX_HTML_PATH "/spiffs/apcfg.html"

//html网页缓存
static char* html_code = NULL;

//当前SSID和密码
static char current_ssid[32] = {0};
static char current_password[64] = {0};

//AP配置事件组
static EventGroupHandle_t apcfg_event = NULL;

//前向声明
static void ws_receive_handle(uint8_t* payload,int len);

/** 从spiffs中加载html页面到内存
 * @param 无
 * @return html页面内容的指针，失败返回NULL
*/
static char* init_web_page_buffer(void){
    //定义挂载点
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",            //挂载点
        .partition_label = "html",         //分区名称
        .max_files = 5,                    //最大打开的文件数
        .format_if_mount_failed = false    //挂载失败是否执行格式化
        };
    //挂载spiffs
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    //查找文件是否存在
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st)){    //stat函数返回非0表示文件不存在
        ESP_LOGE(TAG, "apcfg.html not found");
        return NULL;
    }
    //堆上分配内存，存储html网页
    char* page = (char*)malloc(st.st_size + 1);
    if(!page){   //分配失败
        return NULL;
    }
    memset(page,0,st.st_size + 1);  //清零内存
    //只读方式打开html文件路径
    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (fread(page, st.st_size, 1, fp) == 0){    //读取失败
        free(page); //释放内存
        page = NULL;    //置空指针
        ESP_LOGE(TAG, "fread failed");  //读取失败日志
    }
    fclose(fp); //关闭文件
    return page;    //返回html页面内容的指针
}

/** AP配置任务
 * @param param 任务参数
 * @return 无
*/
static void ap_wifi_task(void* param){
    EventBits_t ev;
    while(1){
        ev = xEventGroupWaitBits(apcfg_event,APCFG_BIT,pdTRUE,pdFALSE,pdMS_TO_TICKS(10*1000));  //等待APCFG_BIT事件，等待10秒超时
        if(ev & APCFG_BIT){
            web_ws_stop();
            wifi_manager_connect(current_ssid,current_password);
        }
    }
}

/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void ap_wifi_init(p_wifi_state_callback f){
    wifi_manager_init(f);  //调用wifi_manager_init初始化wifi
    html_code = init_web_page_buffer();    //加载html网页至内存中
    apcfg_event = xEventGroupCreate();     //创建事件组
    xTaskCreatePinnedToCore(ap_wifi_task,"apcfg",4096,NULL,3,NULL,1);   //创建AP配置任务
}


/** 启动配网模式
 * @param 无
 * @return 无 
*/
void ap_wifi_apcfg(void){
    wifi_manager_ap();  //调用wifi_manager_ap函数进入AP配网模式
    ws_cfg_t ws = { //配置websocket服务器，指定html页面内容和接收回调函数

        .html_code = html_code, //html页面内容
        .receive_fn = ws_receive_handle, //接收回调函数
    };
    web_ws_start(&ws); //启动websocket服务器
}

/** wifi扫描完成回调函数
 * @param numbers 扫描到的ap个数
 * @param ap_records ap信息
 * @return 无 
*/
static void wifi_scan_finish_handle(int numbers,wifi_ap_record_t *ap_records){
    cJSON* root = cJSON_CreateObject();
    cJSON* wifilist_js = cJSON_AddArrayToObject(root,"wifi_list");
    for(int i = 0;i < numbers;i++){    //遍历ap_records，生成对应的JSON格式
        cJSON* wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js,"ssid",(char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(wifi_js,"rssi",ap_records[i].rssi);
        if(ap_records[i].authmode == WIFI_AUTH_OPEN)
            cJSON_AddBoolToObject(wifi_js,"encrypted",0);
        else
            cJSON_AddBoolToObject(wifi_js,"encrypted",1);
        cJSON_AddItemToArray(wifilist_js,wifi_js);
    }
    char* data = cJSON_Print(root);
    ESP_LOGI(TAG,"WS send:%s",data);
    web_ws_send((uint8_t*)data,strlen(data));//生成完JSON字符串后，发送列表数据给客户端
    cJSON_free(data);
    cJSON_Delete(root);
}

/** ws接收回调函数
 * @param payload 数据
 * @param len 数据长度
 * @return 无 
*/
static void ws_receive_handle(uint8_t* payload,int len){
    cJSON* root = cJSON_Parse((char*)payload);
    if(root){
        cJSON* scan_js = cJSON_GetObjectItem(root,"scan");
        cJSON* ssid_js = cJSON_GetObjectItem(root,"ssid");
        cJSON* password_js = cJSON_GetObjectItem(root,"password");
        if(scan_js)    //如果提取到"scan"，说明这个是下发扫描启动的指令，需要启动扫描
        {
            char* scan_value = cJSON_GetStringValue(scan_js);
            if(strcmp(scan_value,"start") == 0)
            {
                wifi_manager_scan(wifi_scan_finish_handle);//启动扫描
            }
        }
        if(ssid_js && password_js)    //如果提取到"ssid"和"password"，说明这个是客户端发来要求连接的SSID和密码
        {
            char* ssid = cJSON_GetStringValue(ssid_js);
            char* password = cJSON_GetStringValue(password_js);
            snprintf(current_ssid,sizeof(current_ssid),"%s",ssid);
            snprintf(current_password,sizeof(current_password),"%s",password);
            ESP_LOGI(TAG,"Receive ssid:%s,password:%s,now stop http server",current_ssid,current_password);
            //此回调函数里面由websocket底层调用，不宜直接调用关闭服务器操作
            xEventGroupSetBits(apcfg_event,APCFG_BIT);  
        }
    }
    else
    {
        ESP_LOGE(TAG,"Receive invaild json");
    }
}
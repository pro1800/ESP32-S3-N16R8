#include "WIFI_Manager.h"
#include "stdio.h"
#include "esp_log.h"
#include "string.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define  MAX_CONNECT_RETAY  6                   //重连次数超过这个值就不再重连了，用户可以根据自己的需求调整这个值
#define TAG "wifi_manager"                      //日志标签

static int sta_connect_count = 0;                           //当前sta模式的重连次数
static bool is_sta_connected = false;                       //当前sta模式的连接状态，只有获取到IP了才认为是连接成功了
static const char* ap_ssid_name = "ESP32-Gavin";            //AP wifi名称
static const char* ap_password = "12345678";                //AP wifi密码
static p_wifi_state_callback wifi_state_cb = NULL;          //用户注册的wifi状态变化回调函数
static esp_netif_t *esp_netif_ap = NULL;                    //AP模式的网络接口对象
static SemaphoreHandle_t scan_sem = NULL;                   //扫描信号量，防止重复扫描x



/**时间回调函数
 * @param arg           用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return              无
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){          //WIFI相关事件
        switch (event_id)                  
        {
        case WIFI_EVENT_STA_START:{         //WIFI以STA模式启动后触发此事件
            // wifi_mode_t mode;               //获取当前的工作模式
            // esp_wifi_get_mode(&mode);       //获取当前的工作模式
            // if(mode == WIFI_MODE_STA)       //如果是STA模式，则开始连接
                esp_wifi_connect();         //启动WIFI连接
            break;
            }
        case WIFI_EVENT_STA_DISCONNECTED:       //WIFI从路由器断开连接后触发此事件
            if (is_sta_connected){              //这里增加这个判断是为了防止重复通知WIFI_STATE_DISCONNECTED这个事件
                // if(wifi_state_cb)
                //     wifi_state_cb(WIFI_STATE_DISCONNECTED);     //通知用户连接断开了
                is_sta_connected = false;                       //更新连接状态
            }
            if(sta_connect_count < MAX_CONNECT_RETAY){          //如果重连次数没有超过最大值，则继续重连
                // wifi_mode_t mode;                               //获取当前的工作模式
                // esp_wifi_get_mode(&mode);                       //获取当前的工作模式
                // if(mode == WIFI_MODE_STA)                       //如果是STA模式，则开始连接
                    esp_wifi_connect();             //继续重连
                sta_connect_count++;                //重连次数加1
            }
            // ESP_LOGI(TAG,"connect to the ap fail,retry now");
            break;
        case WIFI_EVENT_STA_CONNECTED:       //WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG,"Connected to AP");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG,"sta device connected!");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG,"sta device disconnected!");
            break;
        default: break;
        }
    }
    if(event_base == IP_EVENT){                 //IP相关事件
        switch (event_id)                       //这里只处理获取到IP的事件，其他事件不处理
        {
        case IP_EVENT_STA_GOT_IP:               //只有获取到路由器分配的IP，才认为是连上了路由器
            ESP_LOGI(TAG,"GET IP ADDRESS");
            is_sta_connected = true;            //更新连接状态
            if (wifi_state_cb)                  //通知用户连接成功了
            {
                wifi_state_cb(WIFI_STATE_CONNECTED);        //通知用户连接成功了
            }
            break;
        
        default:
            break;
        }
    }
}

/**初始化wifi，默认进入STA模式
 * @param 无
 * @return 无
 */
void wifi_manager_init(p_wifi_state_callback f){        //初始化TCP/IP协议栈，创建默认事件循环，创建默认的STA网络接口对象
    ESP_ERROR_CHECK(esp_netif_init());                  //用于初始化tcpip协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default());   //创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    esp_netif_create_default_wifi_sta();                //使用默认配置创建STA对象
    esp_netif_ap = esp_netif_create_default_wifi_ap();                 //使用默认配置创建AP对象
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();        //使用默认的wifi初始化配置来初始化wifi驱动
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                       //初始化wifi驱动
    //注册事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));        //注册wifi事件回调函数，ESP_EVENT_ANY_ID表示所有wifi事件都会触发这个回调函数
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));       //注册IP事件回调函数，只有获取到IP的事件才会触发这个回调函数
    wifi_state_cb = f;                                                                                      //保存用户注册的wifi状态变化回调函数
    scan_sem = xSemaphoreCreateBinary();                                                                    //创建扫描信号量，防止重复扫描    
    xSemaphoreGive(scan_sem);                           //初始状态给它一个信号，表示可以扫描了
    //启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  //设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_start());                  //启动WIFI
    ESP_LOGI(TAG, "wifi_inti finished.");               //打印日志，表示wifi初始化完成了
}

/**连接WIFI
 * @param ssid
 * @param password
 * @return 成功/失败
 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password){                 //连接WIFI，默认使用WPA2_PSK加密方式，用户可以根据自己的需求调整加密方式和其他参数
    sta_connect_count = 0;                                                              //重置重连次数
    wifi_config_t wifi_config = {                                                       //填充wifi连接配置
        .sta = {                                                                        //STA模式的配置
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,                                   //加密方式
        },
    };
    snprintf((char*)wifi_config.sta.ssid,31,"%s",ssid);                                 //填充SSID名称
    snprintf((char*)wifi_config.sta.password,63,"%s",password);                         //填充密码
    ESP_ERROR_CHECK(esp_wifi_disconnect());                                             //先断开之前的连接，防止连接不上新的路由器了
    wifi_mode_t mode;                                                                   //获取当前的工作模式
    esp_wifi_get_mode(&mode);                                                           //获取当前的工作模式
    if(mode != WIFI_MODE_STA){                                                          //如果当前不是STA模式，则先设置成STA模式，再设置连接配置
        ESP_ERROR_CHECK(esp_wifi_stop());                                               //先停止wifi，才能修改工作模式
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                              //设置工作模式为STA
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));                //设置连接配置
        ESP_ERROR_CHECK(esp_wifi_start());                                              //重新启动wifi
    }
    else{
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));                 //如果已经是STA模式了，则直接设置连接配置就行了
        esp_wifi_connect();                                                             //连接WIFI
    }
    return ESP_OK;
}

/** 进入ap+sta模式
 * @param 无
 * @return 成功/失败
*/
esp_err_t wifi_manager_ap(void){            //进入AP模式，AP模式的SSID和密码在wifi_manager_connect函数中定义了默认值
    wifi_mode_t mode;                       //获取当前的工作模式
    esp_wifi_get_mode(&mode);               //获取当前的工作模式
    if(mode == WIFI_MODE_APSTA)             //需要使用AP+STA模式，才可以执行扫描同时保持客户端连接
        return ESP_OK;
    esp_wifi_disconnect();                  //先断开之前的连接，防止连接不上新的路由器了
    esp_wifi_stop();                        //先停止wifi，才能修改工作模式      
    esp_wifi_set_mode(WIFI_MODE_APSTA);     //设置工作模式为AP+STA  
    wifi_config_t wifi_config =             //填充AP模式的配置，其他参数使用默认值就行了，用户可以根据自己的需求调整其他参数
    {
        .ap = 
        {
            .channel = 5,                   //wifi的通信信道，2.4G wifi一般是1-13
            .max_connection = 2,            //最大连接数，我们用于配网填个小一点的值就行
            .authmode = WIFI_AUTH_WPA2_PSK, //加密方式
        }
    };
    snprintf((char*)wifi_config.ap.ssid,31,"%s",ap_ssid_name);         //填充ap的ssid名称
    wifi_config.ap.ssid_len = strlen(ap_ssid_name);                    //填充ap的ssid长度
    snprintf((char*)wifi_config.ap.password,63,"%s",ap_password);      //填充ap的密码
    esp_wifi_set_config(WIFI_IF_AP,&wifi_config);                      //设置AP模式的配置
    esp_netif_ip_info_t ipInfo;                                                         //如果是AP模式，则需要设置如下网络层信息
    IP4_ADDR(&ipInfo.ip, 192,168,100,1);                                                //本地的IP地址
    IP4_ADDR(&ipInfo.gw, 192,168,100,1);                                                //网关IP地址
    IP4_ADDR(&ipInfo.netmask, 255,255,255,0);                                           //子网掩码
    esp_netif_dhcps_stop(esp_netif_ap);                                                 //设置IP地址前需要停用DHCP服务
    esp_netif_set_ip_info(esp_netif_ap, &ipInfo);                                       //设置IP地址
    esp_netif_dhcps_start(esp_netif_ap);                                                //重新启动DHCP服务
    return esp_wifi_start();
}

/** 扫描任务
 * @param 无
 * @return 成功/失败
*/
static void scan_task(void* param){                                 //扫描任务函数，这个函数会在wifi_manager_scan函数中被创建成一个独立的任务来执行
    p_wifi_scan_callback callback = (p_wifi_scan_callback)param;    //获取用户传入的扫描结果回调函数
    uint16_t ap_count = 0;                                          //实际扫描出的热点数，可能会小于上面定义的最大数        
    uint16_t number = 20;                                           //定义最大扫描出的热点数
    wifi_ap_record_t *ap_info = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t)*number);                //分配内存来保存扫描出的热点列表，用户需要在回调函数中处理完扫描结果后自己释放这块内存
    esp_wifi_scan_start(NULL, true);                                //启动扫描，参数为NULL表示使用默认的扫描配置，第二个参数为true表示这个函数会阻塞直到扫描完成了才会返回
    esp_wifi_scan_get_ap_num(&ap_count);                            //获取实际扫描出的热点数量
    esp_wifi_scan_get_ap_records(&number, ap_info);                 //获取扫描结果列表，参数是扫描结果列表的最大数量和保存扫描结果列表的内存地址，函数会将实际扫描出的热点数量写入到number参数中，可能会小于上面定义的最大数
    ESP_LOGI(TAG, "Total AP count: %d, actual AP number: %d", ap_count, number);     //打印扫描结果数量
    if(callback)                                                    //执行回调函数通知我们扫描结果
        callback(number,ap_info);                                   //回调函数的参数是实际扫描出的热点数量和扫描结果列表，用户需要在回调函数中处理完扫描结果后自己释放这块内存
    free(ap_info);                                                  //释放扫描结果列表的内存
    xSemaphoreGive(scan_sem);                                       //释放信号量
    vTaskDelete(NULL);                                              //任务退出，释放自身资源
    // ESP_LOGI(TAG,"Start wifi scan");

}

/** 启动扫描
 * @param 无
 * @return 成功/失败
*/
esp_err_t wifi_manager_scan(p_wifi_scan_callback f){          
    // if(!scan_sem)             //如果信号量还没有创建，就创建一个信号量
    // {
    //     scan_sem = xSemaphoreCreateBinary();    //创建一个二值信号量，初始状态是空闲的
    //     xSemaphoreGive(scan_sem);               //先给一次信号量，让它变成占用状态，这样就可以在下面的if判断中通过xSemaphoreTake来判断是否有扫描任务在执行了
    // }                   
    if(pdTRUE == xSemaphoreTake(scan_sem,0))    //先看一下是否有扫描任务在执行，防止重复扫描 
    {
        esp_wifi_clear_ap_list();               //清除之前的扫描结果，防止扫描结果列表被之前的结果占满了导致这次扫描没有地方放新的结果了
        // if(pdTRUE == xTaskCreatePinnedToCore(scan_task,"scan",8192,f,3,NULL,1))             //创建一个独立的任务来执行扫描，栈大小8K，优先级3，运行在核心0上，参数是用户传入的扫描结果回调函数
            // return ESP_OK;                                                                  //成功创建了扫描任务了，返回成功
        return xTaskCreatePinnedToCore(scan_task,"scan",8192,f,3,NULL,1);             //创建一个独立的任务来执行扫描，栈大小8K，优先级3，运行在核心0上，参数是用户传入的扫描结果回调函数
    }
    return ESP_OK;      //已经有扫描任务在执行了，返回成功，用户可以根据自己的需求调整这个返回值来区分是成功启动了扫描还是因为有扫描任务在执行了没有启动扫描     
}

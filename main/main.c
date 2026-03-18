#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "button.h"
#include "My_WS2812.h"
#include "WIFI_Manager.h"
#include "ap_wifi.h"

#define TAG "main"


// WIFI状态回调函数
static void wifi_stat_callback(WIFI_STATE state) {
    if (state == WIFI_STATE_CONNECTED) {
        ESP_LOGI(TAG, "WIFI connected");
    }
    if (state == WIFI_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "WIFI disconnected");
    }
}

// ============= GPIO0 按键回调函数（供 button 模块调用）=============

// GPIO0短按回调函数 - 启动呼吸灯
void button_gpio0_short_press_callback(int gpio) {
    ESP_LOGI(TAG, "GPIO0 短按，启动呼吸灯");
    
    // 初始化WS2812
    My_WS2812_Init();
    
    // 启动呼吸灯
    My_WS2812_StartBreathing();
}

// GPIO0释放回调函数 - 停止呼吸灯
void button_gpio0_release_callback(int gpio) {
    ESP_LOGI(TAG, "GPIO0 释放，停止呼吸灯");
    
    // 停止呼吸灯
    My_WS2812_StopBreathing();
}

// GPIO0长按回调函数 (3秒后执行) - 启动AP配网模式
void button_gpio0_long_press_callback(int gpio) {
    ESP_LOGI(TAG, "GPIO0 长按超过3秒，初始化WIFI并启动AP配网模式");
    
    // 停止呼吸灯
    My_WS2812_StopBreathing();
    
    // 关闭LED
    My_WS2812_Init();
    My_WS2812_Light(0, 0, 0);
    
    // 初始化NVS
    nvs_flash_init();
    
    // 初始化AP WIFI功能并启动配网
    ap_wifi_init(wifi_stat_callback);
    wifi_manager_ap();
    ap_wifi_apcfg();  // 进入AP配网模式，启动websocket服务器
}

void app_main(void) {
    // 初始化NVS
    nvs_flash_init();
    
    // 初始化GPIO0
    button_gpio0_init();
    
    // 注册GPIO0按键事件
    button_gpio0_register();
    
    ESP_LOGI(TAG, "应用启动完成，等待按键操作...");
    ESP_LOGI(TAG, "短按GPIO0: 启动呼吸灯");
    ESP_LOGI(TAG, "长按GPIO0 3秒: 启动AP配网模式");
    
    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


/*
// -------------------------------------------------------------
//2026-03-02 Gavin 【RBG LED】

void app_main(void)
{
    My_WS2812_Init();
    while (1)
    {
        My_WS2812_Light(255,0,0);
        vTaskDelay(pdMS_TO_TICKS(80));
        My_WS2812_Light(0,255,0);
        vTaskDelay(pdMS_TO_TICKS(80));
        My_WS2812_Light(0,0,255);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    
}
// -------------------------------------------------------------
*/

/*
// -------------------------------------------------------------
//2026-03-02 Gavin 【RBG LED】
// 硬件配置：严格对应YD-ESP32-S3-COREBOARD V1.4原理图
#define WS2812B_GPIO    48          // RGB灯控制引脚GPIO48
#define RMT_CHANNEL     RMT_CHANNEL_0// RMT通道0（无冲突）
#define LED_NUM         1           // 仅1颗WS2812B
#define BLINK_INTERVAL  1000        // 闪烁间隔(ms)

// WS2812B固定时序参数（勿改，8MHz时钟下1tick=125ns）
#define WS2812B_T0H     12
#define WS2812B_T0L     36
#define WS2812B_T1H     36
#define WS2812B_T1L     12

static const char *TAG = "ESP32S3_WS2812B";
rmt_item32_t ws2812b_data[LED_NUM * 24]; // 24位色值/颗灯珠

// 转换GRB色值为RMT时序信号（WS2812B默认GRB顺序）
static void ws2812b_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t color = (g << 16) | (r << 8) | b;
    rmt_item32_t *item = ws2812b_data;

    for (int i = 23; i >= 0; i--)
    {
        if (color & (1 << i))
        {
            item->level0 = 1; item->duration0 = WS2812B_T1H;
            item->level1 = 0; item->duration1 = WS2812B_T1L;
        }
        else
        {
            item->level0 = 1; item->duration0 = WS2812B_T0H;
            item->level1 = 0; item->duration1 = WS2812B_T0L;
        }
        item++;
    }
    // 发送时序并等待完成
    rmt_write_items(RMT_CHANNEL, ws2812b_data, LED_NUM*24, true);
    rmt_wait_tx_done(RMT_CHANNEL, portMAX_DELAY);
}

// 初始化RMT外设驱动WS2812B
static void ws2812b_init(void)
{
    rmt_config_t rmt_cfg = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_CHANNEL,
        .gpio_num = (gpio_num_t)WS2812B_GPIO,
        .clk_div = 10, // 80MHz/10=8MHz，匹配WS2812B时序
        .tx_config = {
            .carrier_en = false,
            .idle_level = RMT_IDLE_LEVEL_LOW,
            .idle_output_en = true
        }
    };
    rmt_config(&rmt_cfg);
    rmt_driver_install(rmt_cfg.channel, 0, 0);
    ESP_LOGI(TAG, "WS2812B初始化完成 → GPIO%d", WS2812B_GPIO);
}

// FreeRTOS闪烁任务（非阻塞）
static void blink_task(void *arg)
{
    uint8_t color_step = 0;
    while (1)
    {
        switch (color_step)
        {
            case 0: ws2812b_set_color(255,0,0); ESP_LOGI(TAG, "红色"); break;
            case 1: ws2812b_set_color(0,255,0); ESP_LOGI(TAG, "绿色"); break;
            case 2: ws2812b_set_color(0,0,255); ESP_LOGI(TAG, "蓝色"); break;
        }
        color_step = (color_step + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 WS2812B闪烁程序启动");
    ws2812b_init();
    // 创建闪烁任务，栈大小4096，优先级5
    xTaskCreate(blink_task, "blink_task", 4096, NULL, 5, NULL);
}
// -------------------------------------------------------------
*/

/*
// -------------------------------------------------------------
//2026-03-02 Gavin 【信号量】

SemaphoreHandle_t bin_sem;

void taskA(void* parm){
    while (1)
    {
        xSemaphoreGive(bin_sem);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void taskB(void* parm){
    while (1)
    {
        if (pdTRUE == xSemaphoreTake(bin_sem,portMAX_DELAY))
        {
            ESP_LOGI("bin","task B take binsem success");
        }
        
    }
}

void app_main(void)
{
    bin_sem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(taskA,"taskA",2048,NULL,3,NULL,1);
    xTaskCreatePinnedToCore(taskB,"taskB",2048,NULL,4,NULL,1);
}

// -------------------------------------------------------------
*/

/*
// -------------------------------------------------------------
//2026-03-02 Gavin 【队列】

QueueHandle_t Queue_Handle  =   NULL;
typedef struct{
    int value;
}queue_data_t;

void taskA(void* param){
    // 从队列里面接受数据并打印
    queue_data_t data;
    while (1){
        if (pdTRUE == xQueueReceive(Queue_Handle,&data,100)){
            ESP_LOGI("queue","receive queue value:%d",data.value);
        }
    }
}

void taskB(void* param){
    // 从队列里面接受数据并打印
    queue_data_t data;
    memset(&data,0,sizeof(queue_data_t));
    // 每隔一秒向队列发送数据
    while (1){
        xQueueSend(Queue_Handle,&data,100);
        vTaskDelay(pdMS_TO_TICKS(1000));
        data.value++;
    }
}

void app_main(void){
    Queue_Handle    =   xQueueCreate(10,sizeof(queue_data_t));
    xTaskCreatePinnedToCore(taskA,"taskA",2048,NULL,3,NULL,1);
    xTaskCreatePinnedToCore(taskB,"taskB",2048,NULL,3,NULL,1);
}

// -------------------------------------------------------------
*/

/*
// -------------------------------------------------------------
//2026-03-01 Gavin 【每隔500毫秒打印Hello world】

void taskA(void* param){
    while (1)
    {
        ESP_LOGI("main","Hello world!");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    xTaskCreatePinnedToCore(taskA,"helloworld",2048,NULL,3,NULL,1);

}
    
// -------------------------------------------------------------
*/

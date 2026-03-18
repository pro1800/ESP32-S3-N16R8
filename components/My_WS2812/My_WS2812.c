#include "My_WS2812.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WS2812";

static led_strip_handle_t led_strip;

// 呼吸灯任务相关变量
static TaskHandle_t g_breathing_task_handle = NULL;
static bool g_breathing_enabled = false;

// -------------------------------------------------------------
//2026-03-02 Gavin 【RBG LED】

void My_WS2812_Init(void){
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_48, // The GPIO that connected to the LED strip's data line
        .max_leds = 1,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = 10*1000*1000,//LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = 0, //LED_STRIP_MEMORY_BLOCK_WORDS, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = 0, //LED_STRIP_USE_DMA,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle
    // led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    // ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    // return led_strip;
}

void My_WS2812_Light(uint8_t r, uint8_t g, uint8_t b){
    /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
    for (int i = 0; i < 1 ; i++) {  //LED_STRIP_LED_COUNT
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, r, g, b));
     }
    /* Refresh the strip to send data */
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    // ESP_LOGI(TAG, "LED ON!");

}

// 呼吸灯任务函数 - 三种颜色循环交替呼吸
static void ws2812_breathing_task(void *arg) {
    uint8_t colors[3][3] = {
        {255, 0, 0},    // 红色
        {0, 255, 0},    // 绿色
        {0, 0, 255}     // 蓝色
    };
    
    int color_idx = 0;
    
    while (g_breathing_enabled) {
        uint8_t r = colors[color_idx][0];
        uint8_t g_val = colors[color_idx][1];
        uint8_t b = colors[color_idx][2];
        
        // 呼吸灯效果：0->255->0
        for (int brightness = 0; brightness <= 255 && g_breathing_enabled; brightness += 10) {
            My_WS2812_Light(
                (r * brightness) / 255,
                (g_val * brightness) / 255,
                (b * brightness) / 255
            );
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        
        for (int brightness = 255; brightness >= 0 && g_breathing_enabled; brightness -= 10) {
            My_WS2812_Light(
                (r * brightness) / 255,
                (g_val * brightness) / 255,
                (b * brightness) / 255
            );
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        
        // 切换到下一个颜色
        color_idx = (color_idx + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 关闭LED
    My_WS2812_Light(0, 0, 0);
    
    // 删除任务
    g_breathing_task_handle = NULL;
    vTaskDelete(NULL);
}

// 启动呼吸灯
void My_WS2812_StartBreathing(void) {
    if (g_breathing_enabled || g_breathing_task_handle != NULL) {
        ESP_LOGI(TAG, "呼吸灯已经在运行");
        return;
    }
    
    ESP_LOGI(TAG, "启动呼吸灯");
    g_breathing_enabled = true;
    
    // 创建呼吸灯任务
    xTaskCreatePinnedToCore(
        ws2812_breathing_task,
        "breathing_task",
        4096,
        NULL,
        2,
        &g_breathing_task_handle,
        1
    );
}

// 停止呼吸灯
void My_WS2812_StopBreathing(void) {
    if (!g_breathing_enabled && g_breathing_task_handle == NULL) {
        ESP_LOGI(TAG, "呼吸灯已经停止");
        return;
    }
    
    ESP_LOGI(TAG, "停止呼吸灯");
    g_breathing_enabled = false;
    
    // 等待任务自己删除
    for (int i = 0; i < 100 && g_breathing_task_handle != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
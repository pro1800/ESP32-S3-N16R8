#include "onenet_dm.h"
#include "ontnet_mqtt.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "My_WS2812.h"
#include "string.h"
#include "esp_log.h"
#include <stdio.h>

//灯开关状态
static int led_status = 0;

//保存当前灯亮度
static int led_brightness = 0;

//保存WS2812——RGB
static uint8_t ws2812_red = 0;
static uint8_t ws2812_green = 0;
static uint8_t ws2812_blue = 0;

/**
 * 物模型数据初始化
 * @param 无
 * @return 无
 */
void onenet_dm_init(void){
    My_WS2812_Init();
    ledc_timer_config_t led_timer = {
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .timer_num = LEDC_TIMER_0,
    };
    ledc_timer_config(&led_timer);
    ledc_channel_config_t led_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = GPIO_NUM_15,
        .timer_sel = LEDC_TIMER_0,
    };
    ledc_channel_config(&led_channel);
    ledc_fade_func_install(0);
}

/**
 * 处理onenet下行数据
 * @param property_js 包含下行数据json
 * @return 无
 */
void onenet_property_handle(cJSON* property_js){
    cJSON *params_js = cJSON_GetObjectItem(property_js,"params");
    if(params_js)
    {
        cJSON* name_js = params_js->child;
        while(name_js)
        {
            if(strcmp(name_js->string,"LightSwitch") == 0)  //开关数据
            {
                if(cJSON_IsTrue(name_js))    //判断是否打开
                {
                    led_brightness = 50;
                    led_status = 1;
                    int duty = 50*255/100;
                    //设置LED灯光的占空比
                    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty,0);
                }
                else
                {
                    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,0,0);
                    led_status = 0;
                    led_brightness = 0;
                }
            }
            else if(strcmp(name_js->string,"Brightness") == 0)  //亮度数据
            {
                //cJSON_GetNumberValue从一个cJSON的ITEM（键值对)中取出数值类型的值
                led_brightness = cJSON_GetNumberValue(name_js);
                int duty = led_brightness*255/100;
                ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty,0);
            }
            else if(strcmp(name_js->string,"RGBColor") == 0)    //RGB数据
            {
                //取出键名为Red的值
                ws2812_red = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js,"Red"));
                //取出键名为Green的值
                ws2812_green = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js,"Green"));
                //取出键名为Blue的值
                ws2812_blue = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js,"Blue"));
                //设置RGB值
                My_WS2812_Light(ws2812_red, ws2812_green, ws2812_blue);
            }
            name_js = name_js->next;
        }
    }
}

/**
 * 生成上报所有数据的cJSON对象
 * @param 无
 * @return cJSON对象，包含所有属性值
 */
cJSON* onenet_property_upload_dm(void)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"id","123");
    cJSON_AddStringToObject(root,"version","1.0");
    cJSON* params_js = cJSON_AddObjectToObject(root,"params");
    //往params中填充灯开关值
    cJSON* light_js = cJSON_AddObjectToObject(params_js,"LightSwitch");
    cJSON_AddBoolToObject(light_js,"value",led_status);
    //往params中填充灯亮度值
    cJSON* brightness_js = cJSON_AddObjectToObject(params_js,"Brightness");
    cJSON_AddNumberToObject(brightness_js,"value",led_brightness);
    //往params中填充RGB值
    cJSON* color_js = cJSON_AddObjectToObject(params_js,"RGBColor");
    cJSON* color_value_js = cJSON_AddObjectToObject(color_js,"value");
    cJSON_AddNumberToObject(color_value_js,"Red",ws2812_red);
    cJSON_AddNumberToObject(color_value_js,"Green",ws2812_green);
    cJSON_AddNumberToObject(color_value_js,"Blue",ws2812_blue);
    return root;
}
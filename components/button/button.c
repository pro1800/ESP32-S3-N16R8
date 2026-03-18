#include "button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
static const char* TAG = "button";

typedef enum
{
    BUTTON_RELEASE,             //按键没有按下
    BUTTON_PRESS,               //按键按下了，等待一点延时（消抖），然后触发短按回调事件，进入BUTTON_HOLD
    BUTTON_HOLD,                //按住状态，如果时间长度超过设定的超时计数，将触发长按回调函数，进入BUTTON_LONG_PRESS_HOLD
    BUTTON_LONG_PRESS_HOLD,     //此状态等待电平消失，回到BUTTON_RELEASE状态
}BUTTON_STATE;

typedef struct Button
{
    button_config_t btn_cfg;    //按键配置
    BUTTON_STATE    state;      //当前状态
    int press_cnt;              //按下计数
    bool released;              //释放标志，用来追踪是否已经调用过释放回调
    struct Button* next;        //下一个按键参数
}button_dev_t;

//按键处理列表
static button_dev_t *s_button_head = NULL;

//消抖过滤时间
#define FILITER_TIMER   20

//定时器释放运行标志
static bool g_is_timer_running = false;

//定时器句柄
static esp_timer_handle_t g_button_timer_handle;

static void button_handle(void *param);

/** 设置按键事件
 * @param cfg   配置结构体
 * @return ESP_OK or ESP_FAIL 
*/
esp_err_t button_event_set(button_config_t *cfg)
{
    button_dev_t* btn = (button_dev_t*)malloc(sizeof(button_dev_t));
    if(!btn)
        return ESP_FAIL;
    memset(btn,0,sizeof(button_dev_t));
    if(!s_button_head)
    {
        s_button_head = btn;
    }
    else
    {
        button_dev_t* btn_p = s_button_head;
        while(btn_p->next != NULL)
            btn_p = btn_p->next;
        btn_p->next = btn;
    }
    memcpy(&btn->btn_cfg,cfg,sizeof(button_config_t));
    btn->released = true;  //初始化释放标志为真

    if (false == g_is_timer_running) {
        static int timer_interval = 5;
        esp_timer_create_args_t button_timer;
        button_timer.arg = (void*)timer_interval;
        button_timer.callback = button_handle;
        button_timer.dispatch_method = ESP_TIMER_TASK;
        button_timer.name = "button_handle";
        esp_timer_create(&button_timer, &g_button_timer_handle);
        esp_timer_start_periodic(g_button_timer_handle,  5000);
        g_is_timer_running = true;
    }

    return ESP_OK;
}

/** 定时器回调函数，本例中是5ms执行一次
 * @param cfg   配置结构体
 * @return ESP_OK or ESP_FAIL 
*/
static void button_handle(void *param)
{
    int increase_cnt = (int)param;  //传入的参数是5，表示定时器运行周期是5ms
    button_dev_t* btn_target = s_button_head;
    //遍历链表
    for(;btn_target;btn_target = btn_target->next)
    {
        int gpio_num = btn_target->btn_cfg.gpio_num;
        if(!btn_target->btn_cfg.getlevel_cb)
            continue;
        switch(btn_target->state)
        {
            case BUTTON_RELEASE:             //按键没有按下状态
                if(btn_target->btn_cfg.getlevel_cb(gpio_num) == btn_target->btn_cfg.active_level)
                {
                    btn_target->press_cnt += increase_cnt;
                    btn_target->state = BUTTON_PRESS;   //调转到按下状态
                }
                break;
            case BUTTON_PRESS:               //按键按下了，等待一点延时（消抖），然后触发短按回调事件，进入BUTTON_HOLD
                if(btn_target->btn_cfg.getlevel_cb(gpio_num) == btn_target->btn_cfg.active_level)
                {
                    btn_target->press_cnt += increase_cnt;
                    if(btn_target->press_cnt >= FILITER_TIMER)  //过了滤波时间，执行短按回调函数
                    {
                        if(btn_target->btn_cfg.short_cb)
                            btn_target->btn_cfg.short_cb(gpio_num);
                        btn_target->released = false;  //标记已按下
                        btn_target->state = BUTTON_HOLD;    //状态转入按下状态
                    }
                }
                else
                {
                    if(!btn_target->released && btn_target->btn_cfg.release_cb)
                        btn_target->btn_cfg.release_cb(gpio_num);
                    btn_target->released = true;
                    btn_target->state = BUTTON_RELEASE;
                    btn_target->press_cnt = 0;
                }
                break;
            case BUTTON_HOLD:                //按住状态，如果时间长度超过设定的超时计数，将触发长按回调函数，进入BUTTON_LONG_PRESS_HOLD
                if(btn_target->btn_cfg.getlevel_cb(gpio_num) == btn_target->btn_cfg.active_level)
                {
                    btn_target->press_cnt += increase_cnt;
                    if(btn_target->press_cnt >= btn_target->btn_cfg.long_press_time)  //已经检测到按下大于预设长按时间,执行长按回调函数
                    {
                        if(btn_target->btn_cfg.long_cb)
                            btn_target->btn_cfg.long_cb(gpio_num);
                        btn_target->state = BUTTON_LONG_PRESS_HOLD;
                    }
                }
                else
                {
                    if(!btn_target->released && btn_target->btn_cfg.release_cb)
                        btn_target->btn_cfg.release_cb(gpio_num);
                    btn_target->released = true;
                    btn_target->state = BUTTON_RELEASE;
                    btn_target->press_cnt = 0;
                }
                break;
            case BUTTON_LONG_PRESS_HOLD:     //此状态等待电平消失，回到BUTTON_RELEASE状态
                if(btn_target->btn_cfg.getlevel_cb(gpio_num) != btn_target->btn_cfg.active_level)    //检测到释放，就回到初始状态
                {
                    if(!btn_target->released && btn_target->btn_cfg.release_cb)
                        btn_target->btn_cfg.release_cb(gpio_num);
                    btn_target->released = true;
                    btn_target->state = BUTTON_RELEASE;
                    btn_target->press_cnt = 0;
                }
                break;
            default:break;
        }
    }
}

// ============= GPIO0 按键相关函数 =============

#define GPIO0_PIN 0

// GPIO电平读取回调函数
static int gpio0_get_level(int gpio) {
    return gpio_get_level(gpio);
}

// 前向声明 - 外部回调函数（在 main.c 中实现）
extern void button_gpio0_short_press_callback(int gpio);
extern void button_gpio0_release_callback(int gpio);
extern void button_gpio0_long_press_callback(int gpio);

// 初始化GPIO0为输入模式
void button_gpio0_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO0_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "GPIO0 初始化完成");
}

// 注册GPIO0按键事件配置
void button_gpio0_register(void) {
    button_config_t gpio0_cfg = {
        .gpio_num = GPIO0_PIN,
        .active_level = 0,                          // 按下时为低电平
        .long_press_time = 3000,                    // 长按3000ms
        .getlevel_cb = gpio0_get_level,             // 获取电平的回调函数
        .short_cb = button_gpio0_short_press_callback,   // 短按回调函数
        .long_cb = button_gpio0_long_press_callback,     // 长按回调函数
        .release_cb = button_gpio0_release_callback,     // 释放回调函数
    };
    button_event_set(&gpio0_cfg);
    ESP_LOGI(TAG, "GPIO0 按键事件注册完成");
}

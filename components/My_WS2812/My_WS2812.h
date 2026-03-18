#ifndef _My_WS2812_H_
#define _My_WS2812_H_
#include "stdint.h"
#include "driver/rmt_tx.h"  // 新版RMT发送头文件（替换旧的driver/rmt.h）
#include "esp_err.h"

void My_WS2812_Init(void);
void My_WS2812_Light(uint8_t r, uint8_t g, uint8_t b);

// 呼吸灯相关函数
void My_WS2812_StartBreathing(void);
void My_WS2812_StopBreathing(void);

#endif // _MY_WS2812_H_

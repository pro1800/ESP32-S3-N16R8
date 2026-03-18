#ifndef _WS_SERVER_H_
#define _WS_SERVER_H_
#include "esp_err.h"

typedef void(*ws_receive_cb)(uint8_t* payload,int len);

typedef struct{
    const char* html_code;   //网页代码
    ws_receive_cb receive_fn; //接收数据回调函数
}ws_cfg_t;

/**启动WS
 * @param cfg WS配置
 * @return 成功/失败
 */
esp_err_t web_ws_start(ws_cfg_t* cfg);

/**停止WS
 * @param 无
 * @return 成功/失败
 */
esp_err_t web_ws_stop(void);

/**使用websocket协议向客户端发送数据
 * @param data 数据内容
 * @param len 数据长度
 * @return 成功/失败
 */
esp_err_t web_ws_send(uint8_t* data,int len);

#endif
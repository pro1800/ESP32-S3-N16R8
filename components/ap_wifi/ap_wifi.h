#ifndef _AP_WIFI_H_
#define _AP_WIFI_H_
#include "WIFI_Manager.h"
#include "ws_server.h"


/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void ap_wifi_init(p_wifi_state_callback f);

/** 进入AP配网
 * @return 无
*/
void ap_wifi_apcfg(void);


#endif

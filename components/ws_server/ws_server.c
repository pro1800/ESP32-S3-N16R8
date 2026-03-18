#include "ws_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "string.h"

#define TAG "ws_server" // 日志标签

//html页面代码
static const char* http_html = NULL;
//WEBSOCKET接受数据回调函数
static ws_receive_cb ws_receive_fn = NULL;
//客户端FDS
static int client_fds = -1;
//http服务器句柄
static httpd_handle_t server_handle;

/**响应HTTP GET请求的回调函数，这里处理方法就是简单的把HTML网页发回去
 * @param r http请求信息
 * @return ESP_OK / ESP_FAIL
 */
esp_err_t get_http_req(httpd_req_t* r){
    return httpd_resp_send(r,http_html,HTTPD_RESP_USE_STRLEN);
}

/**响应websocket数据的服务函数
 * @param r http请求信息
 * @return ESP_OK / ESP_FAIL
 */
esp_err_t handle_ws_req(httpd_req_t* r){
    //过滤GET请求，GET请求是握手阶段
    if(r->method == HTTP_GET){
        client_fds = httpd_req_to_sockfd(r);
        return ESP_OK;
    }
    httpd_ws_frame_t pkt;   //websocket数据包结构体
    esp_err_t ret;  //接收数据
    memset(&pkt,0,sizeof(pkt)); //清空数据包结构体
    ret = httpd_ws_recv_frame(r,&pkt,0); //获取数据包长度
    if(ret != ESP_OK)
        return ret;
    uint8_t* buf = (uint8_t*)malloc(pkt.len+1); //为数据包内容分配内存
    if(buf == NULL)
        return ESP_FAIL;
    pkt.payload = buf; //设置数据包内容指针
    memset(buf,0,pkt.len+1); //清空数据包内容内存
    ret = httpd_ws_recv_frame(r,&pkt,pkt.len); //接收数据包内容
    if(ret == ESP_OK){
        if(pkt.type == HTTPD_WS_TYPE_TEXT){     //如果数据包类型是文本
            ESP_LOGI(TAG,"Get websocket message:%s",pkt.payload);   //打印接收到的文本消息
            if(ws_receive_fn) //如果设置了数据接收回调函数
                ws_receive_fn(pkt.payload,pkt.len); //调用回调函数处理接收到的数据
        }
    }
    free(buf); //释放数据包内容内存
    return ESP_OK;
}

/**启动WS
 * @param cfg WS配置
 * @return 成功/失败
 */
esp_err_t web_ws_start(ws_cfg_t* cfg){
    if(cfg == NULL)
        return ESP_FAIL;
    http_html = cfg->html_code;//保存HTML代码
    ws_receive_fn = cfg->receive_fn;//保存数据接收回调函数
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); //使用默认配置
    httpd_start(&server_handle,&config); //启动HTTP服务器
    httpd_uri_t uri_get = {
        .uri = "/", //URI根目录路径
        .method = HTTP_GET, //HTTP GET方法
        .handler = get_http_req, //处理HTTP GET请求的回调函数
    };
    httpd_register_uri_handler(server_handle,&uri_get); //注册HTTP GET请求处理函数
    httpd_uri_t uri_ws = {
        .uri = "/ws",//URI路径为/ws
        .method = HTTP_GET, //HTTP GET方法
        .handler = handle_ws_req, //处理websocket数据的回调函数
        .is_websocket = true, //标记这是一个websocket URI
    };
    httpd_register_uri_handler(server_handle,&uri_ws); //注册websocket数据处理函数
    return ESP_OK;
}

/**停止WS服务
 * @param 无
 * @return 成功/失败
 */
esp_err_t web_ws_stop(void){
    if(server_handle){
        httpd_stop(server_handle); //停止HTTP服务器
        server_handle = NULL; //清空服务器句柄
    }
    return ESP_OK;
}

/**使用websocket协议向客户端发送数据
 * @param data 数据内容
 * @param len 数据长度
 * @return 成功/失败
 */
esp_err_t web_ws_send(uint8_t* data,int len){
    httpd_ws_frame_t pkt; //websocket数据包结构体
    memset(&pkt,0,sizeof(pkt)); //清空数据包结构体
    pkt.payload = data; //设置数据包内容指针
    pkt.len = len; //设置数据包内容长度
    pkt.type = HTTPD_WS_TYPE_TEXT; //设置数据包类型为文本
    return httpd_ws_send_data(server_handle,client_fds,&pkt); //发送websocket数据包
}

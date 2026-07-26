#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s_pdm.h"

static const char* TAG = "AUDIO_RECORD";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

/** 初始化喇叭
 * @param bclk 时钟GPIO
 * @param ws 声道线GPIO
 * @param dout 数据GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_speaker(gpio_num_t bclk,gpio_num_t ws,gpio_num_t dout,uint32_t sample_rate)
{
    //i2s总线配置结构体
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
    //i2s参数配置结构体
    i2s_std_config_t i2s_rx_cfg ={
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),    //时钟源
        //通道参数，数据位深是16位，单通道模式
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    //无用
            .bclk = bclk,               //bclk引脚
            .ws = ws,                  //LRCLK引脚
            .dout = dout,              //数据引脚
        },
    };
    //手动切换到右声道，其实这里设不设置无所谓，单声道模式会在两个声道中传输同样的数据
    i2s_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &i2s_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

/** 初始化PDM喇叭
 * @param dat 数据GPIO
 * @param clk 时钟GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_pdm_microphone(gpio_num_t dat,gpio_num_t clk,uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),    //时钟源
        //通道参数，16位数据位深，单通道模式
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = clk,    //clk引脚
            .din = dat,    //DATA引脚
        },
    };
    //选择右声道
    pdm_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

/** 音频输出
 * @param data 16位pcm数据
 * @param samples 写入的数据长度，单位（字）
 * @return 实际写入的数据长度，单位（字）
 */
int audio_write(const int16_t* data, int samples)
{
    size_t bytes_write;
    i2s_channel_write(tx_handle,data,samples*2,&bytes_write,200);
    bytes_write /= 2;
    return bytes_write;
}

/** 录音读取
 * @param data 16位pcm数据
 * @param samples 要求读取的数据长度，单位（字）
 * @return 实际读取的数据长度，单位（字）
 */
int audio_read(int16_t* dest, int samples)
{
    size_t bytes_read;
    i2s_channel_read(rx_handle, dest, samples*2, &bytes_read, 2000);
    bytes_read /= 2;
    return bytes_read;
}

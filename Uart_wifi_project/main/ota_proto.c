#include "ota_proto.h"
#include "app_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "ota_proto"

#define FRAME_HEAD0   0xAA
#define FRAME_HEAD1   0x55
#define TYPE_START    0x01
#define TYPE_DATA     0x02
#define TYPE_END      0x03

#define RSP_OK_START  'K'
#define RSP_OK_FRAME  'A'
#define RSP_ERR       'E'

/* ---------------- CRC ---------------- */

static uint16_t crc16_ccitt(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

/* ---------------- 串口 ---------------- */

void ota_proto_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = APP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(APP_UART_NUM, 4096, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(APP_UART_NUM, APP_UART_TX, APP_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "uart ready: tx=%d rx=%d baud=%d", APP_UART_TX, APP_UART_RX, APP_UART_BAUD);
}

/* ---------------- 帧收发 ---------------- */

static void send_frame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    static uint8_t buf[6 + APP_PROTO_CHUNK + 2];
    uint16_t crc;

    buf[0] = FRAME_HEAD0;
    buf[1] = FRAME_HEAD1;
    buf[2] = type;
    buf[3] = 0x00;
    buf[4] = (uint8_t)(len & 0xFF);
    buf[5] = (uint8_t)(len >> 8);
    if (len) {
        memcpy(&buf[6], payload, len);
    }

    crc = crc16_ccitt(&buf[4], (size_t)len + 2u);
    buf[6 + len]     = (uint8_t)(crc & 0xFF);
    buf[6 + len + 1] = (uint8_t)(crc >> 8);

    uart_write_bytes(APP_UART_NUM, (const char *)buf, (size_t)(6 + len + 2));
}

/*
 * 等到期望的应答字节，中间的其它字节一律跳过。
 *
 * 为什么不能"读到第一个字节就判成败"：
 *   ESP32 启动瞬间 RX 引脚还悬空，可能收到 0x00；上一帧的应答也可能残留在
 *   缓冲里。只要读到一个非目标字节就判失败并重发，STM32 就会把同一块数据
 *   收两遍，整包 CRC32 必然对不上（实测就是这么翻的车）。
 *
 * 返回 0 = 收到 want，1 = 收到 'E'，-1 = 超时
 */
static int wait_for(uint8_t want, int timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int skipped = 0;

    for (;;) {
        int64_t left_us = deadline - esp_timer_get_time();
        uint8_t c = 0;
        int n;

        if (left_us <= 0) {
            return -1;
        }
        n = uart_read_bytes(APP_UART_NUM, &c, 1, pdMS_TO_TICKS((left_us / 1000) + 1));
        if (n != 1) {
            return -1;
        }
        if (c == want) {
            if (skipped > 0) {
                ESP_LOGW(TAG, "跳过 %d 个多余字节后收到 '%c'", skipped, (char)want);
            }
            return 0;
        }
        if (c == RSP_ERR) {
            return 1;
        }
        skipped++;
        if (skipped <= 4) {
            ESP_LOGW(TAG, "忽略多余字节 0x%02X（正在等 '%c'）", c, (char)want);
        }
    }
}

/* 发一帧并等指定应答，失败重试 */
static int send_expect_ack_to(uint8_t type, const uint8_t *payload, uint16_t len,
                              char want, int timeout_ms, int retry)
{
    for (int i = 0; i < retry; i++) {
        send_frame(type, payload, len);
        int r = wait_for((uint8_t)want, timeout_ms);
        if (r == 0) {
            return 0;
        }
        if (r < 0) {
            ESP_LOGW(TAG, "type=0x%02X 第 %d 次: %dms 内没等到 '%c'", type, i + 1, timeout_ms, want);
        } else {
            ESP_LOGW(TAG, "type=0x%02X 第 %d 次: STM32 回 'E'（拒绝），重发", type, i + 1);
        }
    }
    return -1;
}

static int send_expect_ack(uint8_t type, const uint8_t *payload, uint16_t len, char want)
{
    return send_expect_ack_to(type, payload, len, want, APP_ACK_TIMEOUT_MS, APP_RETRY_MAX);
}

/* ---------------- 对外接口 ---------------- */

int ota_proto_start(uint32_t size, uint32_t crc32_or_zero)
{
    uint8_t p[8];

    /* 先清掉复位/启动阶段攒下的噪声字节，从干净状态开始握手 */
    uart_flush_input(APP_UART_NUM);

    p[0] = (uint8_t)(size);
    p[1] = (uint8_t)(size >> 8);
    p[2] = (uint8_t)(size >> 16);
    p[3] = (uint8_t)(size >> 24);

    p[4] = (uint8_t)(crc32_or_zero);
    p[5] = (uint8_t)(crc32_or_zero >> 8);
    p[6] = (uint8_t)(crc32_or_zero >> 16);
    p[7] = (uint8_t)(crc32_or_zero >> 24);

    return send_expect_ack(TYPE_START, p, sizeof(p), RSP_OK_START);
}

int ota_proto_send(const uint8_t *data, uint32_t len)
{
    uint32_t off = 0;

    while (off < len) {
        uint32_t n = len - off;
        if (n > APP_PROTO_CHUNK) {
            n = APP_PROTO_CHUNK;
        }
        /* STM32 侧按半字写入，长度必须是偶数 */
        if (n & 1u) {
            n--;
        }
        if (n == 0) {
            break;
        }
        if (send_expect_ack(TYPE_DATA, &data[off], (uint16_t)n, RSP_OK_FRAME) != 0) {
            return -1;
        }
        off += n;
    }
    return 0;
}

/*
 * END 帧不重试：
 *   STM32 校验并写完 W25Q128 只是一次性动作，写完后会话就关闭了。
 *   若 'A' 在路上丢了再重发 END，STM32 会回 'E'，反而把已成功的升级判成失败。
 *   所以这里只发一次，把等待时间放宽到 APP_END_TIMEOUT_MS（写 32KB 约 1 秒）。
 */
int ota_proto_end(uint32_t crc32)
{
    uint8_t p[4];

    p[0] = (uint8_t)(crc32);
    p[1] = (uint8_t)(crc32 >> 8);
    p[2] = (uint8_t)(crc32 >> 16);
    p[3] = (uint8_t)(crc32 >> 24);

    return send_expect_ack_to(TYPE_END, p, sizeof(p), RSP_OK_FRAME,
                              APP_END_TIMEOUT_MS, 1);
}

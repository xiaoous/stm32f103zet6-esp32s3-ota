#pragma once

/* ==========================================================================
 * 集中配置：改这里就够了
 * ========================================================================== */

/* ---- WiFi ---- */
/* 填你自己的 2.4GHz 账号密码（ESP32 不支持 5GHz）。这两个值不要提交到公开仓库。 */
#define APP_WIFI_SSID "YOUR_WIFI_SSID"
#define APP_WIFI_PASS "YOUR_WIFI_PASSWORD"
#define APP_WIFI_TIMEOUT 20000 /* 连不上时的等待上限 (ms) */

/* ---- 固件地址（局域网 PC 起 http.server 即可）---- */
#define APP_FW_URL      "http://192.168.162.231:8000/P01_led1.bin"
/* ---- STM32 串口 ---- */
#define APP_UART_NUM UART_NUM_1
#define APP_UART_TX 17 /* -> STM32 PA3 (USART2_RX) */
#define APP_UART_RX 18 /* <- STM32 PA2 (USART2_TX) */
#define APP_UART_BAUD 115200

/* ---- 协议参数 ---- */
#define APP_PROTO_CHUNK 2048 /* 每帧最大负载 */
#define APP_ACK_TIMEOUT_MS 3000 /* 等 'A'/'K' 的上限 */
#define APP_RETRY_MAX 5 /* 数据帧重试次数 */
#define APP_END_TIMEOUT_MS 8000 /* END 帧等应答：STM32 要擦+写+回读 W25Q128，约 1 秒 */

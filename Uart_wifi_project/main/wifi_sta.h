#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 连接 WiFi STA，成功返回 0；失败返回 -1（内部会打印原因） */
int wifi_sta_connect(const char *ssid, const char *pass, int timeout_ms);

/* 是否已连上 */
bool wifi_sta_is_connected(void);

#pragma once

/* 从 url 下载固件并推给 STM32；返回 0 成功 */
int ota_client_update(const char *url);

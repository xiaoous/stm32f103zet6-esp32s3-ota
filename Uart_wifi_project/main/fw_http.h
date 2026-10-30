#pragma once

#include <stdint.h>
#include <stddef.h>

/* ==========================================================================
 * fw_http：把 HTTP 固件当成“可顺序读的字节流”
 *
 * 只做三件事：打开（顺便拿到 Content-Length）、读一块、关闭。
 * 不碰协议、不碰串口，方便单独替换成 SD 卡 / LittleFS 等其他来源。
 * ========================================================================== */

typedef struct fw_http fw_http_t;

/*
 * 打开 url，成功返回句柄；失败返回 NULL
 * out_len：服务器返回的 Content-Length（字节数），失败时为 0
 */
fw_http_t *fw_http_open(const char *url, uint32_t *out_len);

/*
 * 读一块：返回 >0 = 读到的字节数；0 = 读完；<0 = 出错
 */
int fw_http_read(fw_http_t *h, uint8_t *buf, size_t cap);

void fw_http_close(fw_http_t *h);

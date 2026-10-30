#pragma once

#include <stdint.h>
#include <stddef.h>

/* ==========================================================================
 * ota_proto：STM32 串口升级协议（只管“帧”，不管固件从哪来）
 *
 * 帧 = AA 55 | type | 00 | len(2,LE) | payload | crc16(2,LE)
 *   crc16 = CRC16-CCITT(0x1021, init 0xFFFF)，覆盖 len + payload
 *   START(0x01): size(4) + crc32(4)；crc32 填 0 表示“整包 CRC32 由 END 帧给出”
 *   DATA (0x02): 数据，长度偶数且 <= APP_PROTO_CHUNK
 *   END  (0x03): crc32(4)；STM32 先比对整包 CRC32，通过才写 W25Q128
 * 应答: 'K' 可发 / 'A' 本帧成功 / 'E' 失败
 *
 * 为什么要有 END：
 *   START 之前就把整包 CRC32 报给 STM32，就得把固件下载两遍（第一遍只为算 CRC）。
 *   改成 END 帧后只下一遍：边发边算 CRC32，发完再把结果交给 STM32 校验。
 * ========================================================================== */

void ota_proto_init(void);

/* 发 START，返回 0 成功；crc32_or_zero 填 0 表示稍后用 ota_proto_end() 给 */
int ota_proto_start(uint32_t size, uint32_t crc32_or_zero);

/* 发一段数据（内部按 CHUNK 拆分），返回 0 成功 */
int ota_proto_send(const uint8_t *data, uint32_t len);

/* 发 END：把整包 CRC32 交给 STM32 校验并落盘。
 * 返回 0 表示 STM32 校验通过且已写完 W25Q128（应答 'A'）*/
int ota_proto_end(uint32_t crc32);

/* zlib 兼容 CRC32 */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);

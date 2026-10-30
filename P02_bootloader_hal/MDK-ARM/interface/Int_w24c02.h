#ifndef __INT_W24C02_H
#define __INT_W24C02_H

#include "i2c.h"
#include "usart.h"
#define W24C02_ADDR 0xA0
#define W24C02_ADDR_R (W24C02_ADDR | 0x01)

#define W24C02_ADDR_SIZE 8
#define W24C02_PAGE_SIZE 16

/**
 * @brief 读取一个字节
 *
 * @param byte_addr
 * @return uint8_t
 */
uint8_t Int_w24c02_read_byte(uint8_t byte_addr);

/**
 * @brief 写入一个字节
 *
 * @param byte_addr
 * @param data
 */
void Int_w24c02_write_byte(uint8_t byte_addr, uint8_t data);

/**
 * @brief 读取多个字节
 *
 */
void Int_w24c02_read_bytes(uint8_t byte_addr, uint8_t *data, uint16_t len);

/**
 * @brief 写入多个字节
 *
 * @param byte_addr
 * @param data
 * @param len
 */
void Int_w24c02_write_bytes(uint8_t byte_addr, uint8_t *data, uint16_t len);

#endif // !__INT_W24C02_H

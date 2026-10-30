#ifndef __INT_W25Q32__
#define __INT_W25Q32__

#include "spi.h"

/* SPI设备在使用的时候片选引脚才需要拉低  平时都需要拉高的
 */

#define W25Q32_READ_ID 0x9F
#define W25Q32_READ_STATUS_REG 0x05
#define W25Q32_READ_DATA 0x03
#define W25Q32_WRITE_DATA 0x02
#define W25Q32_ERASE_SECTOR 0x20
#define W25Q32_WRITE_ENABLE 0X06


/**
 * @brief 拉低片选
 *
 */
void Int_w25q32_start(void);

/**
 * @brief 拉高片选
 *
 */
void Int_w25q32_stop(void);

/**
 * @brief 写入一个字节
 *
 * @param data
 */
void Int_w25q32_write_byte(uint8_t data);

/**
 * @brief 读取一个字节
 *
 * @return uint8_t
 */
uint8_t Int_w25q32_read_byte(void);

/**
 * @brief 读取芯片ID
 *
 * @param mf_id
 * @param device_id
 */
void Int_w25q32_read_id(uint8_t *mf_id, uint16_t *device_id);

/**
 * @brief 读取数据
 *
 * addr: 一共是22位  0x000000 -> 0x3FF   FFF  一次擦除4096字节  一次写入是256字节
 */
// void Int_w25q32_read_data(uint32_t addr, uint8_t *data, uint16_t len);
void Int_w25q32_read_data(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr, uint8_t *data, uint16_t len);

/**
 * @brief 读取数据 使用32位地址
 * 
 * @param addr 
 * @param data 
 * @param len 
 */
void Int_w25q32_read_data_with_32addr(uint32_t addr, uint8_t *data, uint16_t len);

/**
 * @brief 写入数据
 * 
 */
void Int_w25q32_write_data(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr, uint8_t *data, uint16_t len);

/**
 * @brief 使用32位地址直接写入 1次最多写1页
 * 
 * @param addr 
 * @param data 
 * @param len 
 */
void Int_w25q32_write_data_with_32addr(uint32_t addr, uint8_t *data, uint16_t len);
/**
 * @brief 擦除1扇区域
 *  
 */
void Int_w25q32_erase_sector(uint8_t block,uint8_t sector);

#endif // __INT_W25Q32__

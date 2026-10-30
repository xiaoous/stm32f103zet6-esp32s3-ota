#ifndef __INT_W25Q32__
#define __INT_W25Q32__

#include "spi.h"

/* SPI设备在使用的时候片选引脚才需要拉低  平时都需要拉高的
 */

/* ================== W25Q128 芯片参数（原例程是W25Q32，这里改成W25Q128） ================== */
/* 容量: 16M Byte(128M bit)       地址范围: 0x000000 ~ 0xFFFFFF  24位地址              */
/* 页大小: 256字节(写入最小单位)  扇区: 4K字节(擦除最小单位)     块: 64K字节             */
/* 块数量: 256个  每块16个扇区    一次读/写不要超过1页(256字节)                          */
#define W25Q128_CAPACITY         0x01000000U /* 16M Byte */
#define W25Q128_ADDR_MAX         0xFFFFFFU   /* 最后一个有效地址 */
#define W25Q128_PAGE_SIZE        256
#define W25Q128_SECTOR_SIZE      4096
#define W25Q128_BLOCK_SIZE       65536
#define W25Q128_SECTOR_PER_BLOCK 16
#define W25Q128_BLOCK_COUNT      256

/* JEDEC ID: 厂商ID 0xEF(Winbond)  设备ID 0x4018  完整ID 0xEF4018 （W25Q32是0xEF4016） */
#define W25Q128_MF_ID            0xEF
#define W25Q128_DEVICE_ID        0x4018
#define W25Q128_JEDEC_ID         0xEF4018

/* 下面的指令 W25Q32 和 W25Q128 完全一样，宏名保留W25Q32_前缀，其他代码不用改 */

#define W25Q32_READ_ID 0x9F
#define W25Q32_READ_STATUS_REG 0x05
#define W25Q32_READ_DATA 0x03
#define W25Q32_WRITE_DATA 0x02
#define W25Q32_ERASE_SECTOR 0x20
#define W25Q32_WRITE_ENABLE 0X06
#define W25Q32_RELEASE_POWER_DOWN 0XAB  /* 释放掉电模式并读设备ID */
#define W25Q32_RESET_ENABLE 0X66        /* 复位使能 */
#define W25Q32_RESET 0X99               /* 执行复位 */


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
 * @brief 初始化W25Q128 （唤醒 + 软复位 + 校验芯片ID）
 * 
 * @return uint8_t 0:成功(识别到W25Q128)  1:失败
 */
uint8_t Int_w25q32_init(void);

/**
 * @brief 读取数据
 *
 * addr: W25Q128一共是24位  0x000000 -> 0xFFFFFF  一次擦除4096字节(1扇区)  一次写入是256字节(1页)
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
 * @brief 擦除1扇区域
 *  
 */
void Int_w25q32_erase_sector(uint8_t block,uint8_t sector);

#endif // __INT_W25Q32__

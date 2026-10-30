#include "Int_w25q32.h"

/**
 * @brief 拉低片选
 *
 */
void Int_w25q32_start(void)
{
    HAL_GPIO_WritePin(W25Q32_CS_GPIO_Port, W25Q32_CS_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 拉高片选
 *
 */
void Int_w25q32_stop(void)
{
    HAL_GPIO_WritePin(W25Q32_CS_GPIO_Port, W25Q32_CS_Pin, GPIO_PIN_SET);
}

/**
 * @brief 写入一个字节
 *
 * @param data
 */
void Int_w25q32_write_byte(uint8_t data)
{
    HAL_SPI_Transmit(&hspi1, &data, 1, 100);
}

/**
 * @brief 读取一个字节
 *
 * @return uint8_t
 */
uint8_t Int_w25q32_read_byte(void)
{
    uint8_t data;
    // uint8_t dummy = 0xff;
    HAL_SPI_Receive(&hspi1, &data, 1, 100);
    return data;
}

/**
 * @brief 读取芯片ID
 *
 * @param mf_id
 * @param device_id
 */
void Int_w25q32_read_id(uint8_t *mf_id, uint16_t *device_id)
{
    // 1. 拉低片选
    Int_w25q32_start();

    // 2. 发送读取ID指令
    Int_w25q32_write_byte(W25Q32_READ_ID);

    // 3. 读取mf_id
    *mf_id = Int_w25q32_read_byte();
    uint8_t high = Int_w25q32_read_byte();
    uint8_t low = Int_w25q32_read_byte();
    *device_id = high << 8 | low;

    // 4. 拉高片选
    Int_w25q32_stop();
}

// 静态方法 等待芯片忙状态
static void Int_w25q32_wait_busy(void)
{
    // 1. 拉低片选
    Int_w25q32_start();

    // 2. 读取状态寄存器
    while (1)
    {
        Int_w25q32_write_byte(W25Q32_READ_STATUS_REG);
        uint8_t status = Int_w25q32_read_byte();
        // 找到busy是最低位的值  为0表示不忙
        if ((status & 0x01) == 0)
        {
            break;
        }
    }

    // 3. 拉高片选
    Int_w25q32_stop();
}

/**
 * @brief 读取数据
 *
 * addr: 一共是22位  0x000000 -> 0x3F  F  F  FF  一次擦除4096字节  一次写入是256字节
 */
// void Int_w25q32_read_data(uint32_t addr, uint8_t *data, uint16_t len);
void Int_w25q32_read_data(uint8_t block, uint8_t sector, uint8_t page, uint8_t addr, uint8_t *data, uint16_t len)
{
    // 1. 等待忙状态
    Int_w25q32_wait_busy();

    // 2. 拉低片选
    Int_w25q32_start();

    // 3. 发送读取数据指令
    Int_w25q32_write_byte(W25Q32_READ_DATA);
    uint32_t addr_24 = block << 16 | sector << 12 | page << 8 | addr;
    Int_w25q32_write_byte(addr_24 >> 16);
    Int_w25q32_write_byte(addr_24 >> 8);
    Int_w25q32_write_byte(addr_24);

    for (uint16_t i = 0; i < len; i++)
    {
        data[i] = Int_w25q32_read_byte();
    }
    // 4. 拉高片选
    Int_w25q32_stop();
}

/**
 * @brief 读取数据 32位地址
 *
 * @param addr
 * @param data
 * @param len
 */
void Int_w25q32_read_data_with_32addr(uint32_t addr, uint8_t *data, uint16_t len)
{
    // 1. 等待忙状态
    Int_w25q32_wait_busy();

    // 2. 拉低片选
    Int_w25q32_start();

    // 3. 发送读取数据指令
    Int_w25q32_write_byte(W25Q32_READ_DATA);

    Int_w25q32_write_byte((addr >> 16) & 0xff);
    Int_w25q32_write_byte((addr >> 8) & 0xff);
    Int_w25q32_write_byte(addr & 0xff);

    for (uint16_t i = 0; i < len; i++)
    {
        data[i] = Int_w25q32_read_byte();
    }
    // 4. 拉高片选
    Int_w25q32_stop();
}

static void Int_w25q32_write_enable(void)
{
    // 1. 等待忙状态
    Int_w25q32_wait_busy();
    // 2. 拉低片选
    Int_w25q32_start();
    // 3. 发送写使能命令
    Int_w25q32_write_byte(W25Q32_WRITE_ENABLE);
    // 4. 拉低片选
    Int_w25q32_stop();
}

/**
 * @brief 写入数据
 * 假设地址不超出1页的范围
 */
void Int_w25q32_write_data(uint8_t block, uint8_t sector, uint8_t page, uint8_t addr, uint8_t *data, uint16_t len)
{
    // 1.  写使能
    Int_w25q32_write_enable();

    // 2. 拉低片选
    Int_w25q32_start();
    uint32_t addr_24 = block << 16 | sector << 12 | page << 8 | addr;
    Int_w25q32_write_byte(W25Q32_WRITE_DATA);
    Int_w25q32_write_byte(addr_24 >> 16);
    Int_w25q32_write_byte(addr_24 >> 8);
    Int_w25q32_write_byte(addr_24);
    // 3. 写入数据
    for (uint16_t i = 0; i < len; i++)
    {
        Int_w25q32_write_byte(data[i]);
    }
    // 4. 拉高片选
    Int_w25q32_stop();
}

/**
 * @brief 写入数据
 * 假设地址不超出1页的范围
 */
void Int_w25q32_write_data_with_32addr(uint32_t addr, uint8_t *data, uint16_t len)
{
    // 1.  写使能
    Int_w25q32_write_enable();

    // 2. 拉低片选
    Int_w25q32_start();
    Int_w25q32_write_byte(W25Q32_WRITE_DATA);
    Int_w25q32_write_byte((addr >> 16) & 0xff);
    Int_w25q32_write_byte((addr >> 8) & 0xff);
    Int_w25q32_write_byte(addr & 0xff);
    // 3. 写入数据
    for (uint16_t i = 0; i < len; i++)
    {
        Int_w25q32_write_byte(data[i]);
    }
    // 4. 拉高片选
    Int_w25q32_stop();
}

/**
 * @brief 擦除1扇区域
 *
 */
void Int_w25q32_erase_sector(uint8_t block, uint8_t sector)
{
    // 1. 写使能
    Int_w25q32_write_enable();

    // 2. 拉低片选
    Int_w25q32_start();
    // 3. 发送擦除指令
    Int_w25q32_write_byte(W25Q32_ERASE_SECTOR);
    // 4. 发送地址
    Int_w25q32_write_byte(block << 16);
    Int_w25q32_write_byte(sector << 12);
    Int_w25q32_write_byte(0);
    // 5. 拉高片选
    Int_w25q32_stop();
}

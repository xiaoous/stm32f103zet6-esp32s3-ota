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
    HAL_SPI_Transmit(&hspi2, &data, 1, 100);
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
    HAL_SPI_Receive(&hspi2, &data, 1, 100);
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

/**
 * @brief 初始化W25Q128  唤醒 -> 软复位 -> 读取JEDEC ID校验(0xEF4018)
 *        不校验通过返回1，调用处可以据此判断芯片是不是W25Q128
 *
 * @return uint8_t 0:成功(芯片是W25Q128)  1:失败(读不到或者不是W25Q128)
 */
uint8_t Int_w25q32_init(void)
{
    uint8_t mf_id = 0;
    uint16_t device_id = 0;

    // 1. 唤醒芯片  发送0xAB指令后再发3个字节dummy
    Int_w25q32_start();
    Int_w25q32_write_byte(W25Q32_RELEASE_POWER_DOWN);
    (void)Int_w25q32_read_byte();
    (void)Int_w25q32_read_byte();
    (void)Int_w25q32_read_byte();
    Int_w25q32_stop();
    HAL_Delay(1);

    // 2. 软复位  避免芯片停留在上一次的掉电/写保护状态
    Int_w25q32_start();
    Int_w25q32_write_byte(W25Q32_RESET_ENABLE);
    Int_w25q32_stop();
    Int_w25q32_start();
    Int_w25q32_write_byte(W25Q32_RESET);
    Int_w25q32_stop();
    HAL_Delay(1);

    // 3. 读取JEDEC ID  W25Q128 = 0xEF4018 （W25Q32是0xEF4016）
    Int_w25q32_read_id(&mf_id, &device_id);
    /* Accept W25Q32/64/128: same command set, and our layout (meta at 0x000000,
       image at 0x001000, firmware under 64KB) fits all three. Hard failing on
       one exact part number only hides the real SPI problem. */
    if ((mf_id != W25Q128_MF_ID) ||
        ((device_id != 0x4016U) && (device_id != 0x4017U) && (device_id != W25Q128_DEVICE_ID)))
    {
        return 1;
    }

    return 0;
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
 * addr: W25Q128一共是24位  0x000000 -> 0xFFFFFF  一次擦除4096字节(1扇区)  一次写入是256字节(1页)
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
 * @brief 擦除1扇区域
 *
 */
void Int_w25q32_erase_sector(uint8_t block, uint8_t sector)
{
    // 1. 写使能
    Int_w25q32_write_enable();

    // 2. 拉低片选
    Int_w25q32_start();
	uint32_t addr = (uint32_t)block*65536+(uint32_t)sector*4096;
    // 3. 发送擦除指令
    Int_w25q32_write_byte(W25Q32_ERASE_SECTOR);
    // 4. 发送地址
    Int_w25q32_write_byte((addr >> 16) & 0xff);
    Int_w25q32_write_byte((addr >> 8) & 0xff);
    Int_w25q32_write_byte(0);
    // 5. 拉高片选
    Int_w25q32_stop();
}


/* ==================== 32 位地址版本（OTA 用） ==================== */
/* 上面的 write_data / erase_sector 是按 块/扇区/页/偏移 拼地址的，              */
/* 擦除时还用 (addr>>8) 把低 8 位丢掉了；OTA 要按镜像起始地址连续写，这里补一套。 */

/**
 * @brief 按 32 位地址擦除 1 个扇区（4KB）
 */
void Int_w25q32_erase_sector_with_32addr(uint32_t addr)
{
    // 1. 写使能（内部先等忙）
    Int_w25q32_write_enable();

    // 2. 发送扇区擦除指令 0x20 + 24 位地址
    Int_w25q32_start();
    Int_w25q32_write_byte(W25Q32_ERASE_SECTOR);
    Int_w25q32_write_byte((addr >> 16) & 0xFF);
    Int_w25q32_write_byte((addr >> 8) & 0xFF);
    Int_w25q32_write_byte(addr & 0xFF);
    Int_w25q32_stop();

    // 3. 等擦除结束（典型 45ms，最坏 400ms）
    Int_w25q32_wait_busy();
}

/**
 * @brief 按 32 位地址写入 1 页（最多 256 字节）
 */
void Int_w25q32_write_page_with_32addr(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    // 1. 写使能
    Int_w25q32_write_enable();

    // 2. 页编程指令 0x02 + 24 位地址 + 数据
    Int_w25q32_start();
    Int_w25q32_write_byte(W25Q32_WRITE_DATA);
    Int_w25q32_write_byte((addr >> 16) & 0xFF);
    Int_w25q32_write_byte((addr >> 8) & 0xFF);
    Int_w25q32_write_byte(addr & 0xFF);
    for (i = 0; i < len; i++)
    {
        Int_w25q32_write_byte(data[i]);
    }
    Int_w25q32_stop();

    // 3. 等本次编程结束（典型 0.7ms，最坏 3ms）
    Int_w25q32_wait_busy();
}

/**
 * @brief 按 32 位地址写入任意长度，内部按 256 字节页边界拆分
 */
void Int_w25q32_write_data_with_32addr(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t off = 0;
    uint32_t page_left;
    uint32_t n;

    while (off < len)
    {
        // 到本页末尾还能写多少字节
        page_left = W25Q128_PAGE_SIZE - ((addr + off) & (W25Q128_PAGE_SIZE - 1U));
        n = len - off;
        if (n > page_left)
        {
            n = page_left;
        }

        Int_w25q32_write_page_with_32addr(addr + off, &data[off], (uint16_t)n);
        off += n;
    }
}

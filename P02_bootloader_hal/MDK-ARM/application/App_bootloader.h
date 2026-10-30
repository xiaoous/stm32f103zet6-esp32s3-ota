#ifndef __APP_BOOTLOADER_H
#define __APP_BOOTLOADER_H

#include "Int_w24c02.h"
#include "Int_w25q32.h"
#include "Int_bootloader.h"

// 添加校验的秘钥
#define CHECK_KEY_ADDR 0X11
#define CHECK_KEY 0X5A6B
// 存储更新状态的位置
#define CHECK_UPDATE_ADDR 0x10
// 更新状态的值
#define BOOT_UPDATE 0X01
#define BOOT_NO_UPDATE 0X02
// 恢复出厂设置
#define BOOT_RESET 0X03

// 元数据信息的地址
#define META_APP_ADDR_BLOCK 0X00
#define META_APP_ADDR_SECTOR 0X00
#define META_APP_ADDR_PAGE 0X00
#define META_APP_ADDR_ADDR 0X00

// 程序存储的判断条件
#define APP_START_ADDR_MIN 0X001000
#define APP_SIZE_MIN 500
#define APP_SIZE_MAX 0X78000


/**
 * @brief  判断当前是否需要进行更新
 *
 */
void App_bootloader_check_update(void);

/**
 * @brief 检查是否需要恢复出厂设置
 * 
 */
void App_bootloader_check_default(void);

/**
 * @brief 执行更新操作
 *
 */
void App_bootloader_update(void);

/**
 * @brief 执行跳转操作
 *
 */
void App_bootloader_jump_app(void);

#endif // !__APP_BOOTLOADER_H

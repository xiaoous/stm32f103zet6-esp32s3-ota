#ifndef __APP_UPDATE__
#define __APP_UPDATE__

#include "usart.h"
#include "Int_w24c02.h"
#include "Int_w25q32.h"

/*
 * 应用侧 OTA 接收器（运行在 0x08008000）
 *
 * 职责：接收 ESP32-S3 经 USART2 推来的固件，校验通过后写入 W25Q128，
 *       再写元数据与 24C02 标志，最后软复位交给 bootloader 搬运。
 *       另含：流水灯 App_led_poll()、PE3 按键安装 App_update_try_install()。
 *
 * 本模块不擦写内部 flash：应用的代码与中断向量表就在 0x08008000，
 * 擦它等于擦掉自己。搬运一律由 P02（0x08000000）完成。
 *
 * 帧格式与 Uart_wifi_project/main/ota_proto.c 逐字节一致：
 *   AA 55 | type | rsv | len(2,小端) | payload | crc16(2,小端)
 *   crc16 = CRC16-CCITT(0x1021, 初值 0xFFFF)，只覆盖 len 字段 + payload
 *   type 0x01 = START : payload = size(4,小端) + crc32(4,小端)
 *                       crc32 填 0 表示整包 CRC32 由后面的 END 帧给出
 *   type 0x02 = DATA  : payload = 镜像数据，长度 <= 2048
 *   type 0x03 = END   : payload = crc32(4,小端)，校验通过才写 W25Q128
 *   应答：'K' START 接受 / 'A' 本帧成功 / 'E' 本帧失败请重发
 *
 * 写入顺序（铁律）：先校验、后擦写。CRC32 对不上绝不碰 flash，
 * 因为 P02 装完后只校验栈顶/复位向量，不校验内容，坏镜像会被当真装进去。
 */

/* ---- 帧类型 ---- */
#define APP_OTA_HEAD0            0xAAU
#define APP_OTA_HEAD1            0x55U
#define APP_OTA_TYPE_START       0x01U
#define APP_OTA_TYPE_DATA        0x02U
#define APP_OTA_TYPE_END         0x03U

/* ---- 应答 ---- */
#define APP_OTA_RSP_ACCEPT       'K'
#define APP_OTA_RSP_OK           'A'
#define APP_OTA_RSP_ERR          'E'

/* ---- 规模参数 ---- */
#define APP_OTA_MAX_CHUNK        2048U    /* 单帧最大负载，对应 ESP32 的 APP_PROTO_CHUNK */
#define APP_OTA_RING_SIZE        2304U    /* 一帧最长 8 + 2048 = 2056，留余量 */
#define APP_OTA_FRAME_TIMEOUT_MS 8000U    /* idle limit while transferring; must exceed ESP32 APP_ACK_TIMEOUT_MS (3000) */
#define APP_OTA_END_TIMEOUT_MS   8000U    /* 等 END 阶段：W25Q128 擦+写+回读要时间，放宽 */
#define APP_OTA_MIN_SIZE         512U

/* ---- W25Q128 布局（必须与 P02_bootloader_hal 完全一致） ---- */
#define APP_OTA_META_BASE        0x000000U /* 元数据区：起址 4B + 大小 4B（第三步写） */
#define APP_OTA_IMAGE_BASE       0x001000U /* 镜像区：P02 就是从这里往 flash 拷 */
#define APP_OTA_MAX_SIZE         0x78000U  /* 应用区 0x08008000 ~ 0x08080000 */
#define APP_OTA_SECTOR_SIZE      4096U
#define APP_OTA_PAGE_SIZE        256U
/* ---- gates used by P02_bootloader_hal (keep in sync with App_bootloader.h) ---- */
#define APP_OTA_STACK_ADDR       0x20000000U /* P02: (sp & 0xFFFF0000) == STACK_ADDR */
#define APP_OTA_FLASH_START      0x08008000U /* P02: APP_START, install target */
#define APP_OTA_FLASH_END        0x08080000U /* P02: APP_END_ADDR */
#define APP_OTA_CHECK_UPDATE_ADDR 0x10U      /* 24C02: update flag byte */
#define APP_OTA_CHECK_KEY        0x5A6BU     /* 24C02: key at 0x11..0x12, high byte first */
#define APP_OTA_BOOT_UPDATE      0x01U       /* 24C02: flag value meaning install */

/*
 * 整包缓冲大小
 *   F103ZE 只有 64KB SRAM，本工程静态占用约 3KB，再留栈/堆，
 *   这里给 32KB 是安全上限：能装下 32KB 以内的固件。
 *   固件超过这个尺寸时 START 会被拒绝并打印日志，
 *   到时候要么调大这里，要么改成"攒够一扇区写一次"（不再整包缓存）。
 */
#define APP_OTA_IMAGE_BUF        (32U * 1024U)

/**
 * @brief 打开 USART2 接收通道 + 初始化 W25Q128
 *        须在 MX_USART2_UART_Init() / MX_SPI1_Init() 之后调用
 */
void App_update_init(void);

/**
 * @brief 主循环轮询：取环缓 -> 解帧 -> 应答；收到 END 且校验通过才落盘
 */
void App_update_poll(void);
void App_led_poll(void);

#endif

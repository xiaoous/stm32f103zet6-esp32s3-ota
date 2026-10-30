#include "app_update.h"

#include <stdio.h>
#include "main.h"

/* ==================== logging ==================== */

/*
 * APP_OTA_DEBUG 0: one line per milestone, what a normal run needs.
 * APP_OTA_DEBUG 1: full protocol trace, one line per frame class.
 */
#define APP_OTA_DEBUG 0

#if APP_OTA_DEBUG
#define APP_DBG(...)   printf(__VA_ARGS__)
#else
#define APP_DBG(...)   ((void)0)
#endif


/* ==================== 环形接收缓冲 ==================== */

static volatile uint8_t  s_ring[APP_OTA_RING_SIZE];
static volatile uint16_t s_ring_head = 0U;
static volatile uint16_t s_ring_tail = 0U;
static volatile uint8_t  s_ring_overflow = 0U;

static uint8_t s_rx_byte = 0U; /* HAL_UART_Receive_IT 的单字节落点 */

/* ==================== 整包镜像缓冲 ==================== */

/* S3 发来的固件整包先放这里，END 帧校验通过后才写进 W25Q128 */
static uint8_t s_image[APP_OTA_IMAGE_BUF];

/* 回读校验用的小缓冲（不额外占大 RAM） */
static uint8_t s_verify[APP_OTA_PAGE_SIZE];

/* END frame crc32 (4 bytes): kept out of s_image, the image may fill the whole buffer */
static uint8_t s_end_crc[4];

/* ==================== 帧解析状态机 ==================== */

typedef enum
{
    PF_HEAD0 = 0,
    PF_HEAD1,
    PF_TYPE,
    PF_RSV,
    PF_LEN_LO,
    PF_LEN_HI,
    PF_PAYLOAD,
    PF_CRC_LO,
    PF_CRC_HI
} ParseState;

static ParseState s_state = PF_HEAD0;
static uint8_t    s_type = 0U;
static uint16_t   s_len = 0U;
static uint16_t   s_idx = 0U;
static uint16_t   s_crc_calc = 0U;
static uint16_t   s_crc_recv = 0U;

/* ==================== 传输会话 ==================== */

static uint8_t  s_flash_ok = 0U;            /* 1 = W25Q128 初始化成功 */
static uint8_t  s_session = 0U;             /* 1 = START 已接受，正在收数据 */
static uint8_t  s_await_end = 0U;           /* 1 = START 没给 crc32，等 END 帧 */
static uint32_t s_total = 0U;               /* START 声明的镜像大小 */
static uint32_t s_crc32_expect = 0U;        /* 期望的整包 CRC32 */
static uint32_t s_crc32_calc = 0xFFFFFFFFU; /* 流式 CRC32，与 zlib 一致 */
static uint32_t s_recv = 0U;                /* 已接收镜像字节数 */
static uint32_t s_last_ms = 0U;

/* ==================== CRC ==================== */

/* 与 ota_proto.c 的 crc16_ccitt 逐位一致，这里做成可累加的版本 */
static uint16_t crc16_update(uint16_t crc, uint8_t b)
{
    uint8_t i;

    crc ^= (uint16_t)b << 8;
    for (i = 0U; i < 8U; i++)
    {
        crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* zlib 兼容 CRC32：初值 0xFFFFFFFF，结果需再异或 0xFFFFFFFF */
static uint32_t crc32_update(uint32_t crc, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    uint8_t  b;

    for (i = 0U; i < n; i++)
    {
        crc ^= d[i];
        for (b = 0U; b < 8U; b++)
        {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc;
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ==================== 基础收发 ==================== */

static void App_update_reply(uint8_t c)
{
    (void)HAL_UART_Transmit(&huart2, &c, 1U, 100U);
}

static void App_update_reset_parser(void)
{
    s_state = PF_HEAD0;
    s_type = 0U;
    s_len = 0U;
    s_idx = 0U;
    s_crc_calc = 0U;
    s_crc_recv = 0U;
}

static void App_update_abort_session(void)
{
    s_session = 0U;
    s_await_end = 0U;
    s_recv = 0U;
    App_update_reset_parser();
}

/* ==================== 落盘 ==================== */

/**
 * @brief 把 s_image 里的 s_total 字节写进 W25Q128，并回读校验
 * @return 0 成功（回读 crc32 与期望一致）  1 失败
 */
static uint8_t App_update_save_image(void)
{
    uint32_t sectors;
    uint32_t i;
    uint32_t off;
    uint32_t chunk;
    uint32_t crc;

    sectors = (s_total + APP_OTA_SECTOR_SIZE - 1U) / APP_OTA_SECTOR_SIZE;

    printf("[app] write W25Q128: %u bytes @0x%06X, %u sector(s)\r\n",
           (unsigned)s_total, (unsigned)APP_OTA_IMAGE_BASE, (unsigned)sectors);

    /* 1. 先擦干净要用的扇区 */
    for (i = 0U; i < sectors; i++)
    {
        Int_w25q32_erase_sector_with_32addr(APP_OTA_IMAGE_BASE + (i * APP_OTA_SECTOR_SIZE));
    }

    /* 2. 按页写入 */
    Int_w25q32_write_data_with_32addr(APP_OTA_IMAGE_BASE, s_image, s_total);

    /* 3. 回读校验：SPI 写坏是静默的，P02 又不校验内容，只能在这里自己发现 */
    crc = 0xFFFFFFFFU;
    for (off = 0U; off < s_total; off += chunk)
    {
        chunk = s_total - off;
        if (chunk > APP_OTA_PAGE_SIZE)
        {
            chunk = APP_OTA_PAGE_SIZE;
        }
        Int_w25q32_read_data_with_32addr(APP_OTA_IMAGE_BASE + off, s_verify, (uint16_t)chunk);
        crc = crc32_update(crc, s_verify, chunk);
    }
    crc ^= 0xFFFFFFFFU;

    printf("[app] verify: crc32=0x%08X expect=0x%08X %s\r\n",
           (unsigned)crc, (unsigned)s_crc32_expect,
           (crc == s_crc32_expect) ? "OK" : "MISMATCH");

    return (crc == s_crc32_expect) ? 0U : 1U;
}

/**
 * @brief Invalidate the meta record before a transfer starts.
 *        Writing zeros makes the start address 0, which P02 rejects
 *        (< APP_START_ADDR_MIN), so a transfer that loses power midway can
 *        never be installed. Writing 0 only clears bits, so no erase is needed.
 */
static void App_update_clear_meta(void)
{
    uint8_t zero[8] = {0U};

    Int_w25q32_write_data_with_32addr(APP_OTA_META_BASE, zero, 8U);
}

/**
 * @brief Write the meta record P02 reads at W25Q 0x000000:
 *        start address (4B little endian, W25Q source) + size (4B little endian)
 * @return 0 verified, 1 failed
 */
static uint8_t App_update_write_meta(void)
{
    uint8_t  buf[8];
    uint8_t  chk[8];
    uint32_t i;

    buf[0] = (uint8_t)(APP_OTA_IMAGE_BASE);
    buf[1] = (uint8_t)(APP_OTA_IMAGE_BASE >> 8);
    buf[2] = (uint8_t)(APP_OTA_IMAGE_BASE >> 16);
    buf[3] = (uint8_t)(APP_OTA_IMAGE_BASE >> 24);
    buf[4] = (uint8_t)(s_total);
    buf[5] = (uint8_t)(s_total >> 8);
    buf[6] = (uint8_t)(s_total >> 16);
    buf[7] = (uint8_t)(s_total >> 24);

    /* Sector 0 holds only the meta record. NOR can only clear bits, so it
       must be erased first or the new value ends up AND-ed with the old one. */
    Int_w25q32_erase_sector_with_32addr(APP_OTA_META_BASE);
    Int_w25q32_write_data_with_32addr(APP_OTA_META_BASE, buf, 8U);
    Int_w25q32_read_data_with_32addr(APP_OTA_META_BASE, chk, 8U);

    for (i = 0U; i < 8U; i++)
    {
        if (chk[i] != buf[i])
        {
            APP_DBG("[app] meta verify failed at byte %u: 0x%02X != 0x%02X\r\n",
                   (unsigned)i, (unsigned)chk[i], (unsigned)buf[i]);
            return 1U;
        }
    }

    printf("[app] meta ok: start=0x%06X size=%u\r\n",
           (unsigned)APP_OTA_IMAGE_BASE, (unsigned)s_total);
    return 0U;
}

/**
 * @brief Set 24C02[0x10..0x12] = {0x01, 0x5A, 0x6B}, then read it back.
 *        All three bytes go in one page write: P02 rewrites 0x02 and skips the
 *        install whenever the key at 0x11..0x12 does not match.
 * @return 0 verified, 1 failed
 */
static uint8_t App_update_set_flag(void)
{
    uint8_t buf[3];
    uint8_t chk[3];

    buf[0] = APP_OTA_BOOT_UPDATE;
    buf[1] = (uint8_t)(APP_OTA_CHECK_KEY >> 8);
    buf[2] = (uint8_t)(APP_OTA_CHECK_KEY & 0xFFU);

    Int_w24c02_write_bytes(APP_OTA_CHECK_UPDATE_ADDR, buf, 3U);
    HAL_Delay(10U); /* EEPROM needs more than 5ms after a page write */
    Int_w24c02_read_bytes(APP_OTA_CHECK_UPDATE_ADDR, chk, 3U);

    if ((chk[0] != buf[0]) || (chk[1] != buf[1]) || (chk[2] != buf[2]))
    {
        printf("[app] 24C02 readback %02X %02X %02X, expected %02X %02X %02X\r\n",
               (unsigned)chk[0], (unsigned)chk[1], (unsigned)chk[2],
               (unsigned)buf[0], (unsigned)buf[1], (unsigned)buf[2]);
        return 1U;
    }

    printf("[app] 24C02 flag ok: %02X %02X %02X\r\n",
           (unsigned)chk[0], (unsigned)chk[1], (unsigned)chk[2]);
    return 0U;
}

/**
 * @brief 收尾：比对 CRC32 -> 通过才写 W25Q128 -> 应答
 */
static void App_update_commit(uint32_t crc_expect)
{
    uint32_t crc = s_crc32_calc ^ 0xFFFFFFFFU;
    uint32_t stack_ptr;
    uint32_t reset_vec;

    if (crc != crc_expect)
    {
        printf("[app] crc32 mismatch: got 0x%08X expect 0x%08X, flash untouched\r\n",
               (unsigned)crc, (unsigned)crc_expect);
        App_update_reply(APP_OTA_RSP_ERR);
        App_update_abort_session();
        return;
    }

    /* Remember it: App_update_save_image() checks its readback against
       s_crc32_expect, which is still 0 when the CRC came in the END frame. */
    s_crc32_expect = crc_expect;


    /* The same two vector checks P02 performs, done here first: a bad image must
       never get a meta record, because P02 erases the first app page even when
       its own meta check fails (App_bootloader.c ignores the void return). */
    stack_ptr = rd_u32_le(&s_image[0]);
    reset_vec = rd_u32_le(&s_image[4]);
    if (((stack_ptr & 0xFFFF0000U) != APP_OTA_STACK_ADDR) ||
        (reset_vec < APP_OTA_FLASH_START) || (reset_vec > APP_OTA_FLASH_END))
    {
        printf("[app] bad vector: sp=0x%08X reset=0x%08X, not an app for 0x%08X\r\n",
               (unsigned)stack_ptr, (unsigned)reset_vec, (unsigned)APP_OTA_FLASH_START);
        App_update_reply(APP_OTA_RSP_ERR);
        App_update_abort_session();
        return;
    }

    if (App_update_save_image() != 0U)
    {
        printf("[app] save failed, image in flash is NOT valid\r\n");
        App_update_reply(APP_OTA_RSP_ERR);
        App_update_abort_session();
        return;
    }

    /* Meta before flag: the flag is what makes P02 act, so it is only set once
       the meta record P02 depends on has been read back correct. */
    if (App_update_write_meta() != 0U)
    {
        printf("[app] meta failed, 24C02 flag NOT set\r\n");
        App_update_reply(APP_OTA_RSP_ERR);
        App_update_abort_session();
        return;
    }

    /* Stop here on purpose: the image now sits in W25Q128 and nothing has been
       installed. Writing the 24C02 flag is what makes P02 install on the next
       boot, so that step is left to the key press (App_update_try_install).
       The reply still goes out first, so the ESP32 learns the transfer worked. */
    printf("[app] stored in W25Q128: start=0x%06X size=%u, push KEY1 to install\r\n",
           (unsigned)APP_OTA_IMAGE_BASE, (unsigned)s_total);
    App_update_reply(APP_OTA_RSP_OK);
    App_update_abort_session();
}

/* ==================== 帧处理 ==================== */

static void App_update_on_start(void)
{
    uint32_t size;
    uint32_t crc;

    if (s_flash_ok == 0U)
    {
        printf("[app] start: W25Q128 not ready\r\n");
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if (s_len != 8U)
    {
        APP_DBG("[app] start: bad len %u\r\n", (unsigned)s_len);
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }

    size = rd_u32_le(s_image);
    crc  = rd_u32_le(&s_image[4]);

    if ((size < APP_OTA_MIN_SIZE) || (size > APP_OTA_MAX_SIZE) || ((size & 1U) != 0U))
    {
        printf("[app] start: bad size %u\r\n", (unsigned)size);
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if (size > APP_OTA_IMAGE_BUF)
    {
        printf("[app] start: size %u > buffer %u, rejected\r\n",
               (unsigned)size, (unsigned)APP_OTA_IMAGE_BUF);
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }

    s_total = size;
    s_crc32_expect = crc;
    s_await_end = (crc == 0U) ? 1U : 0U; /* START 没给 crc32 就等 END 帧 */
    s_crc32_calc = 0xFFFFFFFFU;
    s_recv = 0U;
    s_session = 1U;
    s_last_ms = HAL_GetTick();

    /* Invalidate the old meta record now, before any erase: if this transfer
       dies halfway, P02 sees a zero start address and installs nothing. */
    App_update_clear_meta();

    printf("[app] start: size=%u crc32=%s\r\n", (unsigned)size,
           (s_await_end != 0U) ? "from END frame" : "given");
    App_update_reply(APP_OTA_RSP_ACCEPT);
}

static void App_update_on_data(void)
{
    if (s_flash_ok == 0U)
    {
        APP_DBG("[app] data: W25Q128 not ready\r\n");
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if (s_session == 0U)
    {
        APP_DBG("[app] data while idle\r\n");
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if ((s_len == 0U) || (s_len > APP_OTA_MAX_CHUNK))
    {
        APP_DBG("[app] data: bad len %u\r\n", (unsigned)s_len);
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if ((s_recv + s_len) > s_total)
    {
        printf("[app] data: over size (%u + %u > %u)\r\n",
               (unsigned)s_recv, (unsigned)s_len, (unsigned)s_total);
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }

    /* 数据在解帧时已直接落在 s_image + s_recv 处，这里只更新账本 */
    s_crc32_calc = crc32_update(s_crc32_calc, &s_image[s_recv], (uint32_t)s_len);
    s_recv += s_len;
    s_last_ms = HAL_GetTick();
    App_update_reply(APP_OTA_RSP_OK);

    if (s_recv == s_total)
    {
        if (s_await_end != 0U)
        {
            /* 镜像收齐，等 END 帧给 CRC32：校验不过绝不落盘 */
            APP_DBG("[app] %u bytes received, waiting END frame for crc32\r\n",
                   (unsigned)s_recv);
        }
        else
        {
            /* 老上位机：START 里已给出 crc32 */
            App_update_commit(s_crc32_expect);
        }
    }
}

static void App_update_on_end(void)
{
    uint32_t crc;

    if (s_session == 0U)
    {
        APP_DBG("[app] end while idle\r\n");
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if (s_len != 4U)
    {
        APP_DBG("[app] end: bad len %u\r\n", (unsigned)s_len);
        App_update_reply(APP_OTA_RSP_ERR);
        return;
    }
    if (s_recv != s_total)
    {
        printf("[app] end: only %u/%u bytes received\r\n", (unsigned)s_recv, (unsigned)s_total);
        App_update_reply(APP_OTA_RSP_ERR);
        App_update_abort_session();
        return;
    }

    crc = rd_u32_le(s_end_crc); /* 4-byte END payload lives in s_end_crc */
    printf("[app] end: crc32=0x%08X\r\n", (unsigned)crc);
    App_update_commit(crc);
}

static void App_update_on_frame(void)
{
    if (s_type == APP_OTA_TYPE_START)
    {
        App_update_on_start();
    }
    else if (s_type == APP_OTA_TYPE_DATA)
    {
        App_update_on_data();
    }
    else if (s_type == APP_OTA_TYPE_END)
    {
        App_update_on_end();
    }
    else
    {
        APP_DBG("[app] unknown type 0x%02X\r\n", (unsigned)s_type);
        App_update_reply(APP_OTA_RSP_ERR);
    }
}

/* ==================== 解帧 ==================== */

static void App_update_feed(uint8_t b)
{
    switch (s_state)
    {
    case PF_HEAD0:
        if (b == APP_OTA_HEAD0)
        {
            s_state = PF_HEAD1;
        }
        break;

    case PF_HEAD1:
        if (b == APP_OTA_HEAD1)
        {
            s_state = PF_TYPE;
        }
        else if (b != APP_OTA_HEAD0)
        {
            s_state = PF_HEAD0;
        }
        break;

    case PF_TYPE:
        s_type = b;
        s_crc_calc = 0xFFFFU; /* crc16 从 len 字段开始累计 */
        s_state = PF_RSV;
        break;

    case PF_RSV:
        /* rsv 不参与 crc16，值也不校验（留给后续协议扩展） */
        s_state = PF_LEN_LO;
        break;

    case PF_LEN_LO:
        s_len = b;
        s_crc_calc = crc16_update(s_crc_calc, b);
        s_state = PF_LEN_HI;
        break;

    case PF_LEN_HI:
        s_len |= (uint16_t)b << 8;
        s_crc_calc = crc16_update(s_crc_calc, b);
        s_idx = 0U;
        /* 负载落在 s_image + s_recv，先确认放得下 */
        if (s_len > APP_OTA_MAX_CHUNK)
        {
            APP_DBG("[app] frame len %u too big\r\n", (unsigned)s_len);
            App_update_reply(APP_OTA_RSP_ERR);
            App_update_reset_parser();
        }
        else if (s_len == 0U)
        {
            App_update_reply(APP_OTA_RSP_ERR);
            App_update_reset_parser();
        }
        else if ((s_type == APP_OTA_TYPE_DATA) &&
                 (((uint32_t)s_recv + (uint32_t)s_len) > APP_OTA_IMAGE_BUF))
        {
            APP_DBG("[app] frame overflow buffer\r\n");
            App_update_reply(APP_OTA_RSP_ERR);
            App_update_reset_parser();
        }
        else
        {
            s_state = PF_PAYLOAD;
        }
        break;

    case PF_PAYLOAD:
        if (s_type == APP_OTA_TYPE_DATA)
        {
            s_image[s_recv + s_idx] = b;
        }
        else if (s_type == APP_OTA_TYPE_START)
        {
            if (s_idx < 8U)
            {
                s_image[s_idx] = b;
            }
        }
        else if (s_type == APP_OTA_TYPE_END)
        {
            if (s_idx < 4U)
            {
                s_end_crc[s_idx] = b;
            }
        }
        s_idx++;
        s_crc_calc = crc16_update(s_crc_calc, b);
        if (s_idx >= s_len)
        {
            s_state = PF_CRC_LO;
        }
        break;

    case PF_CRC_LO:
        s_crc_recv = b;
        s_state = PF_CRC_HI;
        break;

    case PF_CRC_HI:
        s_crc_recv |= (uint16_t)b << 8;
        if (s_crc_recv != s_crc_calc)
        {
            APP_DBG("[app] crc16 mismatch: recv 0x%04X calc 0x%04X (type 0x%02X)\r\n",
                   (unsigned)s_crc_recv, (unsigned)s_crc_calc, (unsigned)s_type);
            App_update_reply(APP_OTA_RSP_ERR);
        }
        else
        {
            App_update_on_frame();
        }
        App_update_reset_parser();
        break;

    default:
        App_update_reset_parser();
        break;
    }
}

/* ==================== 中断侧 ==================== */

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;

    if (huart->Instance != USART2)
    {
        return;
    }

    next = (uint16_t)((s_ring_head + 1U) % APP_OTA_RING_SIZE);
    if (next == s_ring_tail)
    {
        s_ring_overflow = 1U; /* 满了就丢，由 crc16 触发重发 */
    }
    else
    {
        s_ring[s_ring_head] = s_rx_byte;
        s_ring_head = next;
    }

    (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
}

/* ==================== 对外接口 ==================== */


/* ==================== key side (PE3 install request) ==================== */

/*
 * KEY1 (PE3) falling edge only raises a flag.
 * EXTI3 priority is 4 while SysTick is 15, so this handler preempts SysTick:
 * any HAL_Delay() here would wait for a tick that can never arrive.
 */
static volatile uint8_t s_key_install = 0U;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == KEY1_Pin)
    {
        s_key_install = 1U;
    }
}

/*
 * Install the image that already sits in W25Q128.
 *
 * The app must NOT write its own flash: it covers 0x08008000-0x0800BFFF
 * (vector table included), so erasing that range stops instruction fetch and
 * leaves half an image behind. Validate here, raise the 24C02 flag, reset;
 * P02 (running at 0x08000000) does the real erase and copy. The gates below
 * are exactly the ones P02 checks, so what passes here also passes there.
 */
static void App_update_try_install(void)
{
    uint32_t start;
    uint32_t size;
    uint32_t stack_ptr;
    uint32_t reset_vec;

    Int_w25q32_read_data_with_32addr(APP_OTA_META_BASE, s_verify, 8U);
    start = rd_u32_le(&s_verify[0]);
    size  = rd_u32_le(&s_verify[4]);

    if ((start < APP_OTA_IMAGE_BASE) || (size < APP_OTA_MIN_SIZE) || (size > APP_OTA_MAX_SIZE))
    {
        printf("[app] key: no valid image in W25Q128 (start=0x%08X size=%u)\r\n",
               (unsigned)start, (unsigned)size);
        return;
    }

    Int_w25q32_read_data_with_32addr(start, s_verify, 8U);
    stack_ptr = rd_u32_le(&s_verify[0]);
    reset_vec = rd_u32_le(&s_verify[4]);

    if (((stack_ptr & 0xFFFF0000U) != APP_OTA_STACK_ADDR) ||
        (reset_vec < APP_OTA_FLASH_START) || (reset_vec > APP_OTA_FLASH_END))
    {
        printf("[app] key: bad vector sp=0x%08X reset=0x%08X, not an app for 0x%08X\r\n",
               (unsigned)stack_ptr, (unsigned)reset_vec, (unsigned)APP_OTA_FLASH_START);
        return;
    }

    printf("[app] key: image ok (start=0x%08X size=%u), set flag and reset\r\n",
           (unsigned)start, (unsigned)size);

    if (App_update_set_flag() != 0U)
    {
        printf("[app] key: 24C02 flag failed, P02 will not install\r\n");
        return;
    }

    HAL_Delay(300);
    NVIC_SystemReset();
}


/* ==================== LEDs: flowing light, non blocking ==================== */

#define APP_LED_STEP_MS 200U

static uint32_t s_led_ms    = 0U;
static uint8_t  s_led_step  = 0U;
static uint8_t  s_led_drawn = 0U;
static uint8_t  s_led_quiet = 0U; /* 1 = a session is running, both LEDs off */

/*
 * Alternates LED0 (PB5) and LED1 (PE5) every 200 ms, and turns both off while
 * an OTA session is active.
 *
 * Must stay non blocking: at 115200 a 2048-byte frame takes 178 ms, and the
 * receive ring holds only 2304 bytes, so any HAL_Delay() here would overflow
 * the ring and drop frames. HAL_GetTick() only.
 *
 * Both LEDs are active low (gpio.c clears them with GPIO_PIN_SET on init).
 */
void App_led_poll(void)
{
    if (s_session != 0U)
    {
        if (s_led_quiet == 0U)
        {
            s_led_quiet = 1U;
            s_led_drawn = 0U;
            HAL_GPIO_WritePin(LED_GPIO_Port,  LED_Pin,  GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
        }
        return;
    }

    if (s_led_quiet != 0U)
    {
        /* session finished (aborted or done): redraw on the next call */
        s_led_quiet = 0U;
    }

    if ((s_led_drawn != 0U) && ((HAL_GetTick() - s_led_ms) < APP_LED_STEP_MS))
    {
        return;
    }

    s_led_drawn = 1U;
    s_led_ms = HAL_GetTick();
    s_led_step = (uint8_t)((s_led_step + 1U) % 2U);

    if (s_led_step == 0U)
    {
        HAL_GPIO_WritePin(LED_GPIO_Port,  LED_Pin,  GPIO_PIN_RESET); /* LED0 on  */
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);   /* LED1 off */
    }
    else
    {
        HAL_GPIO_WritePin(LED_GPIO_Port,  LED_Pin,  GPIO_PIN_SET);
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }
}

void App_update_init(void)
{
    uint8_t  mf_id = 0U;
    uint16_t dev_id = 0U;

    s_ring_head = 0U;
    s_ring_tail = 0U;
    s_ring_overflow = 0U;
    s_session = 0U;
    s_await_end = 0U;
    s_recv = 0U;
    App_update_reset_parser();

    /* W25Q128：唤醒 + 软复位 + JEDEC ID(0xEF4018) 校验 */
    Int_w25q32_read_id(&mf_id, &dev_id);
    APP_DBG("[app] W25Q JEDEC: mf=0x%02X id=0x%04X\r\n", (unsigned)mf_id, (unsigned)dev_id);

    if (Int_w25q32_init() == 0U)
    {
        s_flash_ok = 1U;
        printf("[app] flash ready (%s), image base 0x%06X, buffer %u bytes\r\n",
               (dev_id == 0x4018U) ? "W25Q128 16MB" :
               (dev_id == 0x4017U) ? "W25Q64 8MB" :
               (dev_id == 0x4016U) ? "W25Q32 4MB" : "unknown part",
               (unsigned)APP_OTA_IMAGE_BASE, (unsigned)APP_OTA_IMAGE_BUF);
    }
    else
    {
        s_flash_ok = 0U;
        printf("[app] flash NOT usable (mf=0x%02X id=0x%04X), OTA disabled\r\n",
               (unsigned)mf_id, (unsigned)dev_id);
    }

    HAL_NVIC_SetPriority(USART2_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);

    printf("[app] ota comm ready: USART2 115200, waiting for S3\r\n");
}

void App_update_poll(void)
{
    uint16_t guard = APP_OTA_RING_SIZE;
    uint8_t  b;
    uint32_t timeout;

    if ((s_key_install != 0U) && (s_session == 0U))
    {
        s_key_install = 0U;
        App_update_try_install();
    }

    while ((s_ring_tail != s_ring_head) && (guard > 0U))
    {
        b = s_ring[s_ring_tail];
        s_ring_tail = (uint16_t)((s_ring_tail + 1U) % APP_OTA_RING_SIZE);
        App_update_feed(b);
        guard--;
    }

    if (s_ring_overflow != 0U)
    {
        s_ring_overflow = 0U;
        printf("[app] rx ring overflow, drop\r\n");
    }

    if (s_session != 0U)
    {
        /* 收齐后等 END（要留出写盘的时间），传数据阶段按 3s 判超时 */
        timeout = ((s_await_end != 0U) && (s_recv == s_total)) ? APP_OTA_END_TIMEOUT_MS
                                                              : APP_OTA_FRAME_TIMEOUT_MS;
        if ((HAL_GetTick() - s_last_ms) > timeout)
        {
            printf("[app] session timeout at %u/%u\r\n", (unsigned)s_recv, (unsigned)s_total);
            App_update_abort_session();
        }
    }
}

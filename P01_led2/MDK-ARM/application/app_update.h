#ifndef __APP_UPDATE__
#define __APP_UPDATE__

#include "usart.h"

// 程序状态机
typedef enum
{
    UPDATE_IDLE = 0,
    UPDATE_RECV_SEND_CMD,
    UPDATE_RECV_DATA,
    UPDATE_RECV_CHECK_DATA,
    UPDATE_RECV_BOOT_UPDATE,
    UPDATE_END
} Update_State_t;

/**
 * @brief 循环调用 执行状态机逻辑
 *
 */
void App_update_work(void);

#endif // __APP_UPDATE__

#include "app_update.h"

Update_State_t update_state = UPDATE_IDLE;

static void App_run(void)
{
    // 闪烁LED1  2.0程序 改成灯闪烁
    HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    HAL_Delay(500);
}

/**
 * @brief 循环调用 执行状态机逻辑
 *
 */
void App_update_work(void)
{
    switch (update_state)
    {
    case UPDATE_IDLE:
        // 只有不需要进行更新程序的时候  才会运行程序之前的功能
        App_run();
        break;
    default:
        break;
    }
}

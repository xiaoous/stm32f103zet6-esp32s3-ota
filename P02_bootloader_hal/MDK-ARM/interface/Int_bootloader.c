#include "Int_bootloader.h"

/**
 * @brief 跳转到A程序
 * uint8_t: 0:成功 1:失败
 */
uint8_t Int_bootloader_jump_to_app(uint32_t app_start_addr)
{

    typedef void (*pFunc)(void);
    // 1. 校验
    // 栈顶地址的值
    uint32_t app_stack_ptr = *(volatile uint32_t *)(app_start_addr);
    uint32_t app_reset_handle = *(volatile uint32_t *)(app_start_addr + 4);

    // 1.1 校验栈顶地址
    if ((app_stack_ptr & 0xFFFF0000) != STACK_ADDR)
    {
        printf("stack addr error\n");
        return 1;
    }

    // 1.2 校验复位中断地址
    if (app_reset_handle < app_start_addr || app_reset_handle > APP_END_ADDR)
    {
        printf("reset handle error\n");
        return 1;
    }

    // 2. 注销boot loader程序
    NVIC_DisableIRQ(EXTI9_5_IRQn);
    NVIC_DisableIRQ(USART1_IRQn);

    // 关闭systick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    // 2.1 关闭HAL库 注销掉外设的配置  不会注销内核
    // HAL_DeInit();

    // 2.1 关闭中断
    __disable_irq();

    // 2.2 设置堆栈指针
    __set_MSP(app_stack_ptr);

    // 2.3 重定向中断向量表
    SCB->VTOR = app_start_addr;

    // 2.4 跳转到A程序复位中断
    pFunc jump_to_app = (pFunc)app_reset_handle;
    // 跳转代码之后的内容是执行不到的
    jump_to_app();

    return 0;
}

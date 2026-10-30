#include "Int_can.h"
#include <string.h>
/**
 * @brief 配置白名单，手动开启can 初始化CAN
 *
 */
void Int_can_Init(void)
{
    // 1.配置过滤器，
    CAN_FilterTypeDef FilterConfig = {0};
    FilterConfig.FilterBank = 0;
    FilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    // 填写寄存器
    FilterConfig.FilterIdHigh = 0x0000;
    FilterConfig.FilterIdLow = 0x0000;
    // 只要掩码都是0 表示ID不需要匹配上  掩码哪一位是1  表示哪一位需要匹配上
    FilterConfig.FilterMaskIdHigh = 0x0000;
    FilterConfig.FilterMaskIdLow = 0x0000;
    // 使用哪一位队列
    FilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    FilterConfig.FilterActivation = ENABLE;

    HAL_CAN_ConfigFilter(&hcan, &FilterConfig);
    // 2.手动开启CAN
    HAL_CAN_Start(&hcan);
}

/**
 * @brief 发送CAN消息
 * id:消息ID
 * data:数据指针
 * len:数据长度
 */
void Int_can_send(uint16_t id, uint8_t *data, uint8_t len)
{
    // 等待邮箱空闲
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0)
        ;
    // 将发送的数据发送到邮箱
    CAN_TxHeaderTypeDef TxHeader = {0};
    TxHeader.StdId = id;         // 标准ID  29位ID
    TxHeader.IDE = CAN_ID_STD;   // 标准格式
    TxHeader.RTR = CAN_RTR_DATA; // 数据帧
    TxHeader.DLC = len;          // 数据长度

    uint32_t TxMailbox = 0;
    // 12.CAN信息    2.CAN信息的头信息     3.数据 4.邮箱编号
    HAL_CAN_AddTxMessage(&hcan, &TxHeader, data, &TxMailbox);
}


void Int_can_Receive_message(CAM_Res_MSG *res_msg ,uint8_t *msg_count)
{
   *msg_count = HAL_CAN_GetRxFifoFillLevel(&hcan,CAN_RX_FIFO0);
  for(uint8_t i = 0;i<*msg_count;i++)
   {
    //指向当前使用的缓存
    CAM_Res_MSG *res_msg_temp = &res_msg[i];
    //清空当前缓存中的数据
    memset(res_msg_temp,0,sizeof(CAM_Res_MSG));
     //读取一条消息
     HAL_CAN_GetRxMessage(&hcan,CAN_RX_FIFO0,&(res_msg_temp->RxHeader),res_msg_temp->Data);
   }
}

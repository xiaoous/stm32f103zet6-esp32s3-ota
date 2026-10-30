#ifndef __INT_CAN_H__
#define __INT_CAN_H__

#include "can.h"

typedef struct
{
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t Data[8];
}CAM_Res_MSG;

/**
 * @brief 配置白名单，手动开启can 初始化CAN
 * 
 */
void Int_can_Init(void);

/**
 * @brief 发送CAN消息
 * id:消息ID
 * data:数据指针
 * len:数据长度
 */
void Int_can_send(uint16_t id,uint8_t *data, uint8_t len);


void Int_can_Receive_message(CAM_Res_MSG *res_msg ,uint8_t *msg_count);

#endif // __INT_CAN_H__

/***************************************************************
 * bsp_usart_platform.h
 * USART 平台适配宏 — 所有 AT32 相关操作集中于此
 ***************************************************************/
#ifndef BSP_USART_PLATFORM_H
#define BSP_USART_PLATFORM_H

#include "at32f403a_407_conf.h"

/* ====== 类型定义（跨平台统一） ====== */
#define BSP_USART_TypeDef               usart_type

/* ====== 发送相关 ====== */
#define BSP_USART_IS_TX_EMPTY(husart)   (usart_flag_get((BSP_USART_TypeDef*)(husart), USART_TDBE_FLAG) != RESET)
#define BSP_USART_SEND_BYTE(husart, b)  usart_data_transmit((BSP_USART_TypeDef*)(husart), (b))

/* ====== 接收相关 ====== */
#define BSP_USART_IS_RX_READY(husart)   (usart_flag_get((BSP_USART_TypeDef*)(husart), USART_RDBF_FLAG) != RESET)
#define BSP_USART_RECV_BYTE(husart)     usart_data_receive((BSP_USART_TypeDef*)(husart))

/* ====== 标志清除 ====== */
#define BSP_USART_CLR_RX_FLAG(husart)   usart_flag_clear((BSP_USART_TypeDef*)(husart), USART_RDBF_FLAG)
#define BSP_USART_CLR_ERR_FLAG(husart)  usart_flag_clear((BSP_USART_TypeDef*)(husart), USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG | USART_PERR_FLAG)

/* ====== 中断控制 ====== */
#define BSP_USART_ENABLE_IT(husart, it) usart_interrupt_enable((BSP_USART_TypeDef*)(husart), (it), TRUE)
#define BSP_USART_DISABLE_IT(husart,it) usart_interrupt_enable((BSP_USART_TypeDef*)(husart), (it), FALSE)

#endif

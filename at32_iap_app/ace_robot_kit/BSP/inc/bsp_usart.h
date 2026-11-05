/***************************************************************
 * bsp_usart.h
 * USART 通用接口（平台无关）
 ***************************************************************/
#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdint.h>

/* USART 句柄 */
typedef struct {
    void *usart_handle;         /* USART 外设基地址（如 UART5） */
} BSP_USART_Handle_t;

/**
 * @brief 初始化 USART 句柄，在任务启动前调用；硬件由 wk_usart 初始化
 * @param handle  句柄指针
 * @param usart   USART 外设基地址
 */
void BSP_USART_Init(BSP_USART_Handle_t *handle, void *usart);

/**
 * @brief 发送单字节（阻塞）
 */
void BSP_USART_SendByte(BSP_USART_Handle_t *handle, uint8_t byte);

/**
 * @brief 发送数据块（阻塞）
 */
void BSP_USART_SendBuffer(BSP_USART_Handle_t *handle, const uint8_t *buf, uint16_t len);

/**
 * @brief 保护 UART 发送的递归互斥锁（仅任务上下文）
 */
void BSP_USART_TxLock(void);
void BSP_USART_TxUnlock(void);

/**
 * @brief 读取接收到的字节（需先判断是否有数据）
 */
uint8_t BSP_USART_ReceiveByte(BSP_USART_Handle_t *handle);

/**
 * @brief 查询接收数据是否就绪
 */
uint8_t BSP_USART_IsRxReady(BSP_USART_Handle_t *handle);

/**
 * @brief 清除接收标志
 */
void BSP_USART_ClrRxFlag(BSP_USART_Handle_t *handle);

/**
 * @brief 清除错误标志
 */
void BSP_USART_ClrErrFlag(BSP_USART_Handle_t *handle);

#endif

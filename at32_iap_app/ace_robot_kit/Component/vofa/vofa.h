/***************************************************************
 * vofa.h
 * VOFA数据通信协议模块（平台无关）
 * - 使用 BSP_USART_Handle_t 接口，不依赖任何AT32 HAL
 * - 中断处理由APP层统一调度
 ***************************************************************/
#ifndef VOFA_H
#define VOFA_H

#include <stdint.h>
#include <stdbool.h>
#include "bsp_usart.h"          /* 使用B层USART接口 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 配置参数 ============================ */
#define VOFA_MAX_CHANNELS           25      /* 最大通道数 */
#define VOFA_RX_BUFFER_SIZE         128     /* 接收缓冲区大小 */
#define VOFA_CMD_BUF_SIZE           10      /* 命令缓冲区大小 */

/* ============================ 类型定义 ============================ */

/* VOFA数据格式枚举 */
typedef enum {
    VOFA_FORMAT_JUSTFLOAT = 0,      /* JustFloat 二进制格式 */
    VOFA_FORMAT_FIREWATER           /* FireWater 文本格式 */
} VofaFormat_t;

/* VOFA句柄结构体（平台无关） */
typedef struct {
    /* ---- 串口通信（通过BSP层） ---- */
    BSP_USART_Handle_t *usart;      /* BSP USART句柄指针 */

    /* ---- 数据格式与通道 ---- */
    VofaFormat_t format;            /* 数据格式 */
    uint8_t      channel_count;     /* 通道数量 */
    float        channel_data[VOFA_MAX_CHANNELS];          /* 通道数据 */
    char         channel_names[VOFA_MAX_CHANNELS][16];     /* 通道名称 */

    /* ---- 发送控制 ---- */
    uint32_t     last_send_time;    /* 上次发送时间(ms) */
    uint32_t     send_interval;     /* 发送间隔(ms) */

    /* ---- 接收缓冲区 ---- */
    uint8_t      rx_buffer[VOFA_RX_BUFFER_SIZE];   /* 接收缓冲区 */
    uint16_t     rx_index;          /* 缓冲区索引 */
    bool         rx_ready;          /* 接收完成标志 */

    /* ---- 命令解析结果 ---- */
    char         cmd[VOFA_CMD_BUF_SIZE];    /* 命令标识 */
    float        value;             /* 对应的浮点数值 */
} VofaHandle_t;

/* ============================ API函数声明 ============================ */

/* ------------ 初始化与配置 ------------ */

/**
 * @brief  初始化VOFA模块
 * @param  handle       VOFA句柄指针
 * @param  usart        BSP USART句柄指针（由APP层传入）
 * @param  format       数据格式
 * @param  interval_ms  发送间隔(ms)，0表示立即发送
 */
void VOFA_Init(VofaHandle_t *handle, BSP_USART_Handle_t *usart,VofaFormat_t format);

/**
 * @brief  设置通道名称
 */
void VOFA_SetChannelName(VofaHandle_t *handle, uint8_t ch, const char *name);

/**
 * @brief  设置通道数值
 */
void VOFA_SetChannelValue(VofaHandle_t *handle, uint8_t ch, float value);

/**
 * @brief  获取通道数值
 */
float VOFA_GetChannelValue(VofaHandle_t *handle, uint8_t ch);

/* ------------ 数据发送 ------------ */

/**
 * @brief  发送数据（按照设置的格式发送所有通道数据）
 */
void VOFA_SendData(VofaHandle_t *handle);

/**
 * @brief  发送自定义数据数组
 */
void VOFA_SendCustomData(VofaHandle_t *handle, const float *data,
                         uint8_t count, VofaFormat_t format);

/**
 * @brief  发送字符串（直接透传）
 */
void VOFA_SendString(VofaHandle_t *handle, const char *str);

/* ------------ 数据接收（供中断处理调用） ------------ */

/**
 * @brief  接收单字节数据（在串口中断中调用）
 * @param  handle  VOFA句柄指针
 * @param  byte    接收到的字节
 */
void VOFA_ReceiveByte(VofaHandle_t *handle, uint8_t byte);

/**
 * @brief  检查是否接收到完整数据
 * @return true: 数据就绪
 */
bool VOFA_IsDataReady(VofaHandle_t *handle);

/**
 * @brief  解析接收到的命令（支持M、C、F等多种指令）
 * @note   APP层在检测到数据就绪后调用此函数解析
 */
void VOFA_ParseCommand(VofaHandle_t *handle);

/**
 * @brief  获取解析后的命令字符串
 */
const char *VOFA_GetCommand(VofaHandle_t *handle);

/**
 * @brief  获取解析后的数值
 */
float VOFA_GetValue(VofaHandle_t *handle);

/**
 * @brief  重置接收状态（清空缓冲区）
 */
void VOFA_ResetRx(VofaHandle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* VOFA_H */

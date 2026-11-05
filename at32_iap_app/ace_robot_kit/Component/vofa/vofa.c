/***************************************************************
 * vofa.c
 * VOFA数据通信协议模块实现
 * - 所有串口操作通过 BSP_USART_* 接口
 * - 无任何AT32 HAL直接调用
 ***************************************************************/
#include "vofa.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// 定义浮点数格式（FireWater使用）的默认精度
#define VOFA_FLOAT_FORMAT "%.3f"

/* ============================ 私有辅助函数 ============================ */

/**
 * @brief  发送单字节（通过BSP层）
 */
static void vofa_send_byte(VofaHandle_t *handle, uint8_t byte)
{
    if (handle == NULL || handle->usart == NULL) return;
    BSP_USART_SendByte(handle->usart, byte);
}

/**
 * @brief  发送JustFloat格式数据
 */
static void vofa_send_justfloat(VofaHandle_t *handle)
{
    if (handle == NULL) return;

    /* 发送所有通道数据（小端字节序） */
    for (uint8_t i = 0; i < handle->channel_count; i++) {
        uint8_t *bytes = (uint8_t *)&handle->channel_data[i];
        vofa_send_byte(handle, bytes[0]);
        vofa_send_byte(handle, bytes[1]);
        vofa_send_byte(handle, bytes[2]);
        vofa_send_byte(handle, bytes[3]);
    }

    /* 发送帧尾 */
    vofa_send_byte(handle, 0x00);
    vofa_send_byte(handle, 0x00);
    vofa_send_byte(handle, 0x80);
    vofa_send_byte(handle, 0x7F);
}

/**
 * @brief  发送FireWater格式数据
 */
static void vofa_send_firewater(VofaHandle_t *handle)
{
    if (handle == NULL) return;

    char temp_buf[32];

    for (uint8_t i = 0; i < handle->channel_count; i++) {
        if (i > 0) {
            vofa_send_byte(handle, ',');
        }
        int len = snprintf(temp_buf, sizeof(temp_buf), VOFA_FLOAT_FORMAT,
                          handle->channel_data[i]);
        for (int j = 0; j < len; j++) {
            vofa_send_byte(handle, (uint8_t)temp_buf[j]);
        }
    }

    vofa_send_byte(handle, '\n');
}

/* ============================ API实现 ============================ */

void VOFA_Init(VofaHandle_t *handle, BSP_USART_Handle_t *usart,VofaFormat_t format)
{
    if (handle == NULL) return;

    memset(handle, 0, sizeof(VofaHandle_t));

    handle->usart         = usart;
    handle->format        = format;
    handle->channel_count = 0;

    /* 设置默认通道名称 */
    for (int i = 0; i < VOFA_MAX_CHANNELS; i++) {
        snprintf(handle->channel_names[i], 16, "CH%d", i);
    }
}

void VOFA_SetChannelName(VofaHandle_t *handle, uint8_t ch, const char *name)
{
    if (handle == NULL || ch >= VOFA_MAX_CHANNELS || name == NULL) return;

    strncpy(handle->channel_names[ch], name, 15);
    handle->channel_names[ch][15] = '\0';

    if (ch >= handle->channel_count) {
        handle->channel_count = ch + 1;
    }
}

void VOFA_SetChannelValue(VofaHandle_t *handle, uint8_t ch, float value)
{
    if (handle == NULL || ch >= VOFA_MAX_CHANNELS) return;

    handle->channel_data[ch] = value;

    if (ch >= handle->channel_count) {
        handle->channel_count = ch + 1;
    }
}

float VOFA_GetChannelValue(VofaHandle_t *handle, uint8_t ch)
{
    if (handle == NULL || ch >= VOFA_MAX_CHANNELS) return 0.0f;
    return handle->channel_data[ch];
}

/* ------------ 数据发送 ------------ */

void VOFA_SendData(VofaHandle_t *handle)
{
    if (handle == NULL) return;

    switch (handle->format) {
        case VOFA_FORMAT_JUSTFLOAT:
            vofa_send_justfloat(handle);
            break;

        case VOFA_FORMAT_FIREWATER:
            vofa_send_firewater(handle);
            break;

        default:
            break;
    }
}

void VOFA_SendCustomData(VofaHandle_t *handle, const float *data,
                         uint8_t count, VofaFormat_t format)
{
    if (handle == NULL || data == NULL || count == 0) return;

    if (format == VOFA_FORMAT_JUSTFLOAT) {
        for (uint8_t i = 0; i < count; i++) {
            uint8_t *bytes = (uint8_t *)&data[i];
            vofa_send_byte(handle, bytes[0]);
            vofa_send_byte(handle, bytes[1]);
            vofa_send_byte(handle, bytes[2]);
            vofa_send_byte(handle, bytes[3]);
        }
        vofa_send_byte(handle, 0x00);
        vofa_send_byte(handle, 0x00);
        vofa_send_byte(handle, 0x80);
        vofa_send_byte(handle, 0x7F);

    } else if (format == VOFA_FORMAT_FIREWATER) {
        char temp_buf[32];
        for (uint8_t i = 0; i < count; i++) {
            if (i > 0) vofa_send_byte(handle, ':');
            int len = snprintf(temp_buf, sizeof(temp_buf), VOFA_FLOAT_FORMAT, data[i]);
            for (int j = 0; j < len; j++) {
                vofa_send_byte(handle, (uint8_t)temp_buf[j]);
            }
        }
        vofa_send_byte(handle, '\n');
    }
}

void VOFA_SendString(VofaHandle_t *handle, const char *str)
{
    if (handle == NULL || str == NULL) return;
    while (*str) {
        vofa_send_byte(handle, (uint8_t)(*str++));
    }
}

/* ------------ 数据接收 ------------ */

void VOFA_ReceiveByte(VofaHandle_t *handle, uint8_t byte)
{
    if (handle == NULL) return;

    /* 缓冲区满则重置 */
    if (handle->rx_index >= VOFA_RX_BUFFER_SIZE - 1) {
        handle->rx_index = 0;
        handle->rx_ready = false;
        return;
    }

    handle->rx_buffer[handle->rx_index++] = byte;

    /* 检查结束标志 */
    if (byte == '\n' || byte == '\r') {
        if (handle->rx_index > 0 && handle->rx_index < VOFA_RX_BUFFER_SIZE) {
            handle->rx_buffer[handle->rx_index - 1] = '\0';
        }
        handle->rx_ready = true;
    }
}

bool VOFA_IsDataReady(VofaHandle_t *handle)
{
    if (handle == NULL) return false;
    return handle->rx_ready;
}

void VOFA_ParseCommand(VofaHandle_t *handle)
{
    if (handle == NULL || !handle->rx_ready) return;

    /* 格式: X=value (如 M2=12.5, C1=0.3, F3=1.0) */
    char *equal_pos = strchr((char *)handle->rx_buffer, '=');

    if (equal_pos != NULL) {
        int cmd_len = equal_pos - (char *)handle->rx_buffer;

        if (cmd_len > 0 && cmd_len < VOFA_CMD_BUF_SIZE - 1) {
            strncpy(handle->cmd, (char *)handle->rx_buffer, cmd_len);
            handle->cmd[cmd_len] = '\0';

            handle->value = atof(equal_pos + 1);
        }
    }

    /* 重置接收状态 */
    VOFA_ResetRx(handle);
}

// 返回命令标识和数值的接口，供指令映射模块调用
const char *VOFA_GetCommand(VofaHandle_t *handle)
{
    if (handle == NULL) return "";
    return handle->cmd;
}

// 获取解析到的数值
float VOFA_GetValue(VofaHandle_t *handle)
{
    if (handle == NULL) return 0.0f;
    return handle->value;
}

void VOFA_ResetRx(VofaHandle_t *handle)
{
    if (handle == NULL) return;
    handle->rx_index = 0;
    handle->rx_ready = false;
    memset(handle->rx_buffer, 0, VOFA_RX_BUFFER_SIZE);
}

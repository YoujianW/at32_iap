#ifndef APP_UPDATE_FW_TASK_H
#define APP_UPDATE_FW_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 协议：UPFW(55 50 46 57) + BIN长度(4字节小端) + CRC32(4字节小端)。
 * 收到 READY\r\n 后，每次发送 1024 字节 BIN 数据，末块按实际剩余长度发送。
 * 每块等待 OK\r\n 后再继续，最后一个 OK 表示校验成功并即将复位。
 * CRC32：调用 CRC32_Calc，初值 0x3AA3，不取反，只计算 BIN 原始数据。
 * 收到 ERROR\r\n 或超时后，重新开始升级，不重发单个数据块。
 */
void APP_Update_FW_ReceiveByte(uint8_t byte);
void Update_FW_Task_func(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif

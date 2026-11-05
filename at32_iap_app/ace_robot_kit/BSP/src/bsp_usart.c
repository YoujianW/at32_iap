#include "bsp_usart.h"
#include "bsp_usart_platform.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * 全局 UART 发送互斥保护：
 * - 防止多个任务通过 BSP 接口并发访问串口
 * - 由于 BSP_USART_SendByte 是阻塞发送，这里用 FreeRTOS 互斥锁序列化
 */
static SemaphoreHandle_t xBspUartTxMutex = NULL;

static void BSP_USART_TxMutexEnsure(void)
{
    /* 确保互斥锁只创建一次 */
    if (xBspUartTxMutex == NULL) {
        xBspUartTxMutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(xBspUartTxMutex != NULL);
    }
}

void BSP_USART_Init(BSP_USART_Handle_t *handle, void *usart) {
    if (handle == NULL || usart == NULL) return;
    handle->usart_handle = usart;
    /* 初始化时顺便确保 TX 互斥锁存在 */
    if (__get_IPSR() != 0) return;
    BSP_USART_TxMutexEnsure();
}

void BSP_USART_TxLock(void)
{
    if (__get_IPSR() != 0 || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
    /* BSP_USART_Init 应在任务启动前调用，避免多个任务同时创建锁。 */
    configASSERT(xBspUartTxMutex != NULL);
    if (xBspUartTxMutex == NULL) {
        return;
    }
    /* 只有调度器启动后才能使用 FreeRTOS 互斥锁 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTakeRecursive(xBspUartTxMutex, portMAX_DELAY);
    }
}

void BSP_USART_TxUnlock(void)
{
    if (__get_IPSR() != 0 || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
    if (xBspUartTxMutex == NULL) {
        return;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGiveRecursive(xBspUartTxMutex);
    }
}

void BSP_USART_SendByte(BSP_USART_Handle_t *handle, uint8_t byte) {
    if (handle == NULL || handle->usart_handle == NULL) return;
    /* 发送前加锁，避免多个发送者同时操作串口 */
    BSP_USART_TxLock();
    while (!BSP_USART_IS_TX_EMPTY(handle->usart_handle));
    BSP_USART_SEND_BYTE(handle->usart_handle, byte);
    BSP_USART_TxUnlock();
}


void BSP_USART_SendBuffer(BSP_USART_Handle_t *handle, const uint8_t *buf, uint16_t len) {
    if (handle == NULL || handle->usart_handle == NULL || buf == NULL) return;
    /* 数据块整体加锁，内部逐字节递归加锁。 */
    BSP_USART_TxLock();
    for (uint16_t i = 0; i < len; i++) {
        BSP_USART_SendByte(handle, buf[i]);
    }
    BSP_USART_TxUnlock();
}

uint8_t BSP_USART_ReceiveByte(BSP_USART_Handle_t *handle) {
    if (handle == NULL || handle->usart_handle == NULL) return 0;
    return BSP_USART_RECV_BYTE(handle->usart_handle);
}

uint8_t BSP_USART_IsRxReady(BSP_USART_Handle_t *handle) {
    if (handle == NULL || handle->usart_handle == NULL) return 0;
    return BSP_USART_IS_RX_READY(handle->usart_handle);
}

void BSP_USART_ClrRxFlag(BSP_USART_Handle_t *handle) {
    if (handle == NULL || handle->usart_handle == NULL) return;
    BSP_USART_CLR_RX_FLAG(handle->usart_handle);
}

void BSP_USART_ClrErrFlag(BSP_USART_Handle_t *handle) {
    if (handle == NULL || handle->usart_handle == NULL) return;
    BSP_USART_CLR_ERR_FLAG(handle->usart_handle);
}

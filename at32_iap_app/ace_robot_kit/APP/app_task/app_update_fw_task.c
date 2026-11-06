#include "app_task.h"
#include "bsp_flash.h"
#include "bsp_usart.h"
#include "crc_check.h"
#include <string.h>

#define APP_UPDATE_FLAG_ADDR     0x08005800U
#define APP_UPDATE_LENGTH_ADDR   0x08005810U
#define APP_UPDATE_FLAG          0xAA5555AAU
#define APP_UPDATE_INFO_SIZE     0x800U
#define APP_UPDATE_DATA_ADDR     0x08060000U
#define APP_UPDATE_MAX_SIZE      0x5A000U
#define APP_UPDATE_BLOCK_SIZE    1024U
#define APP_UPDATE_HEADER_SIZE   12U
#define APP_UPDATE_TIMEOUT_MS    5000U
#define APP_UPDATE_CRC_INIT      0x3AA3U

static BSP_USART_Handle_t update_usart;
static BSP_FLASH_Handle_t update_flash;
static uint8_t rx_buffer[APP_UPDATE_BLOCK_SIZE];
static volatile uint16_t rx_count;
static volatile uint16_t rx_size;
static volatile uint8_t rx_ready = 1;

static void update_reply(const char *reply)
{
    BSP_USART_SendBuffer(&update_usart, (const uint8_t *)reply, strlen(reply));
}

static void update_receive_start(uint16_t size)
{
    rx_ready = 1;
    rx_size = size;
    rx_count = 0;
    rx_ready = 0;
}

static int update_write_block(uint32_t offset, uint16_t size)
{
    /* 每页首次写入前擦除，末包补 0xFF 到 4 字节。 */
    if (offset % update_flash.sector_size == 0 &&
        BSP_FLASH_Erase(&update_flash, APP_UPDATE_DATA_ADDR + offset,
                        update_flash.sector_size) != 0)
    {
        return -1;
    }
    memset(rx_buffer + size, 0xFF, ((size + 3U) & ~3U) - size);
    if (BSP_FLASH_Write(&update_flash, APP_UPDATE_DATA_ADDR + offset,
                        rx_buffer, (size + 3U) & ~3U) != 0)
    {
        return -1;
    }

    /* 回读暂存数据，用于计算实际写入固件的 CRC。 */
    return BSP_FLASH_Read(&update_flash, APP_UPDATE_DATA_ADDR + offset,
                           rx_buffer, size);
}

static int update_commit(uint32_t length)
{
    uint32_t flag = APP_UPDATE_FLAG;

    if (BSP_FLASH_Erase(&update_flash, APP_UPDATE_FLAG_ADDR,
                        APP_UPDATE_INFO_SIZE) != 0 ||
        BSP_FLASH_Write(&update_flash, APP_UPDATE_LENGTH_ADDR,
                        (const uint8_t *)&length, sizeof(length)) != 0)
    {
        return -1;
    }

    /* 最后写入升级标志。 */
    return BSP_FLASH_Write(&update_flash, APP_UPDATE_FLAG_ADDR,
                            (const uint8_t *)&flag, sizeof(flag));
}

/* 中断只接收数据，收满后等待任务处理。 */
void APP_Update_FW_ReceiveByte(uint8_t byte)
{
    if (rx_ready) return;
    rx_buffer[rx_count++] = byte;
    if (rx_count == rx_size) rx_ready = 1;
}

void Update_FW_Task_func(void *pvParameters)
{
    uint32_t length;
    uint32_t expected_crc;
    uint32_t crc;
    uint32_t offset;
    uint16_t size;
    TickType_t start_tick;

    (void)pvParameters;

    BSP_USART_Init(&update_usart, USART1);
    BSP_FLASH_Init(&update_flash, APP_UPDATE_FLAG_ADDR);
    update_receive_start(APP_UPDATE_HEADER_SIZE);

    while (1)
    {
        /* 1. 等中断收齐请求头，每次等待 1 ms，让其他任务运行。 */
        while (!rx_ready) vTaskDelay(pdMS_TO_TICKS(1));

        /* 2. 解析请求头：[UPFW 4 字节][BIN 长度 4 字节][CRC32 4 字节]，数值为小端序。 */
        if (memcmp(rx_buffer, "UPFW", 4) != 0)
        {
            update_receive_start(APP_UPDATE_HEADER_SIZE);
            continue; // 帧头不匹配，回到 while 开头等待新的请求头
        }
        memcpy(&length, rx_buffer + 4, sizeof(length));
        memcpy(&expected_crc, rx_buffer + 8, sizeof(expected_crc));
        /* BIN 长度必须在 1 字节到 360 KB 之间。 */
        if (length == 0 || length > APP_UPDATE_MAX_SIZE)
        {
            update_receive_start(APP_UPDATE_HEADER_SIZE);
            update_reply("ERROR\r\n");
            continue; // 长度无效，结束本次请求并重新等待请求头
        }

        /* 3. 分块接收整包 BIN，每成功处理一块后推进 offset。 */
        crc = APP_UPDATE_CRC_INIT;
        for (offset = 0; offset < length; offset += size)
        {
            /* 中间块为 1024 字节，最后一块按剩余长度接收。 */
            size = (length - offset > APP_UPDATE_BLOCK_SIZE)
                   ? APP_UPDATE_BLOCK_SIZE : length - offset;

            /* 先准备接收，再应答，保证上位机收到回应后可以立即发数据。
             * 第一轮 READY 允许发送首块；后续 OK 确认上一块并允许发送本块。
             */
            update_receive_start(size);
            start_tick = xTaskGetTickCount();
            update_reply(offset == 0 ? "READY\r\n" : "OK\r\n");
            /* 等待当前块收齐，超过 5 秒则结束本次升级。 */
            while (!rx_ready &&
                   xTaskGetTickCount() - start_tick < pdMS_TO_TICKS(APP_UPDATE_TIMEOUT_MS))
            {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            /* 超时或暂存区擦写、读回失败，跳出 for，进入后面的失败处理。 */
            if (!rx_ready || update_write_block(offset, size) != 0) break;

            /* 累计 Flash 读回数据的 CRC，上一块结果作为本块初值。 */
            crc = CRC32_Calc(rx_buffer, size, crc);
        }

        /* 4. 所有块处理完且 CRC 一致后，写入长度，最后写升级标志。 */
        if (offset == length && crc == expected_crc && update_commit(length) == 0)
        {
            update_reply("OK\r\n"); // 最终 OK：校验与提交成功，Bootloader 尚未搬运
            while (usart_flag_get(USART1, USART_TDC_FLAG) == RESET); // 等待最终应答真正发完
            BSP_FLASH_SystemReset(); // 复位，由 Bootloader 搬运固件并启动新 APP
        }
        else
        {
            /* 本次升级失败，恢复请求头接收，并通知上位机停止发送。 */
            update_receive_start(APP_UPDATE_HEADER_SIZE);
            update_reply("ERROR\r\n");
        }
    }
}

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
        while (!rx_ready) vTaskDelay(pdMS_TO_TICKS(1));

        /* 帧头为 UPFW，长度和 CRC32 均为 4 字节小端序。 */
        if (memcmp(rx_buffer, "UPFW", 4) != 0)
        {
            update_receive_start(APP_UPDATE_HEADER_SIZE);
            continue;
        }
        memcpy(&length, rx_buffer + 4, sizeof(length));
        memcpy(&expected_crc, rx_buffer + 8, sizeof(expected_crc));
        if (length == 0 || length > APP_UPDATE_MAX_SIZE)
        {
            update_receive_start(APP_UPDATE_HEADER_SIZE);
            update_reply("ERROR\r\n");
            continue;
        }

        crc = APP_UPDATE_CRC_INIT;
        for (offset = 0; offset < length; offset += size)
        {
            size = (length - offset > APP_UPDATE_BLOCK_SIZE)
                   ? APP_UPDATE_BLOCK_SIZE : length - offset;

            /* 先准备缓冲区，再允许上位机发送下一块。 */
            update_receive_start(size);
            start_tick = xTaskGetTickCount();
            update_reply(offset == 0 ? "READY\r\n" : "OK\r\n");
            while (!rx_ready &&
                   xTaskGetTickCount() - start_tick < pdMS_TO_TICKS(APP_UPDATE_TIMEOUT_MS))
            {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            if (!rx_ready || update_write_block(offset, size) != 0) break;

            crc = CRC32_Calc(rx_buffer, size, crc);
        }

        if (offset == length && crc == expected_crc && update_commit(length) == 0)
        {
            update_reply("OK\r\n");
            while (usart_flag_get(USART1, USART_TDC_FLAG) == RESET);
            BSP_FLASH_SystemReset();
        }
        else
        {
            update_receive_start(APP_UPDATE_HEADER_SIZE);
            update_reply("ERROR\r\n");
        }
    }
}

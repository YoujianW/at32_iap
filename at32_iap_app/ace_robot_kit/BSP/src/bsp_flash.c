/***************************************************************
 * bsp_flash.c
 * Flash BSP 层实现
 ***************************************************************/
#include "bsp_flash.h"
#include "bsp_flash_platform.h"
#include <string.h>

/* 使用减法检查边界，避免 address + size 溢出。 */
static int BSP_FLASH_RangeValid(const BSP_FLASH_Handle_t *handle,
                                uint32_t address, size_t size)
{
    uint32_t end = BSP_FLASH_BASE + BSP_FLASH_TOTAL_SIZE;

    if (handle == NULL || size == 0) return 0;
    if (handle->base_address < BSP_FLASH_BASE || handle->base_address >= end) return 0;
    if (address < handle->base_address || address >= end) return 0;
    return size <= (size_t)(end - address);
}

void BSP_FLASH_Init(BSP_FLASH_Handle_t *handle, uint32_t base_address)
{
    if (handle == NULL) return;
    handle->base_address = base_address;
    handle->sector_size  = BSP_FLASH_SECTOR_SIZE;
}

int BSP_FLASH_Erase(const BSP_FLASH_Handle_t *handle,
                    uint32_t address, size_t size)
{
    if (!BSP_FLASH_RangeValid(handle, address, size)) return -1;

    /*
     * Sector 对齐：
     *   start = address 向下对齐到 sector 边界
     *   end   = address + size 向上对齐
     */
    uint32_t start = BSP_FLASH_SECTOR_BASE(address);
    uint32_t end   = BSP_FLASH_SECTOR_BASE(address + (uint32_t)size
                                           + BSP_FLASH_SECTOR_SIZE - 1);

    if (start < handle->base_address) return -1;

    BSP_FLASH_UNLOCK();

    for (uint32_t sector = start; sector < end; sector += BSP_FLASH_SECTOR_SIZE) {
        if (BSP_FLASH_SECTOR_ERASE(sector) != FLASH_OPERATE_DONE) {
            BSP_FLASH_LOCK();
            return -1;
        }
    }

    BSP_FLASH_LOCK();
    return 0;
}

int BSP_FLASH_Write(const BSP_FLASH_Handle_t *handle,
                    uint32_t address, const uint8_t *data, size_t size)
{
    if (data == NULL || !BSP_FLASH_RangeValid(handle, address, size)) return -1;
    if (address % BSP_FLASH_PROG_UNIT != 0) return -1;
    if (size % BSP_FLASH_PROG_UNIT != 0)    return -1;

    BSP_FLASH_UNLOCK();

    /*
     * 按 32-bit 字写入 Flash。
     * 将 4 字节打包为一个 uint32_t（小端序），调用 word_program。
     */
    size_t word_count = size / sizeof(uint32_t);

    for (size_t i = 0; i < word_count; i++) {
        uint32_t word;
        /* 源缓冲区可以未对齐，不能直接转换为 uint32_t 指针。 */
        memcpy(&word, data + i * sizeof(uint32_t), sizeof(word));
        if (BSP_FLASH_WORD_PROGRAM(address + i * sizeof(uint32_t),
                                   word) != FLASH_OPERATE_DONE) {
            BSP_FLASH_LOCK();
            return -1;
        }
    }

    BSP_FLASH_LOCK();
    return 0;
}

int BSP_FLASH_Read(const BSP_FLASH_Handle_t *handle,
                   uint32_t address, uint8_t *buf, size_t size)
{
    if (buf == NULL || !BSP_FLASH_RangeValid(handle, address, size)) return -1;

    /*
     * AT32 Flash 是内存映射的，直接 memcpy 即可。
     * 这比逐字节循环快得多（编译器会用 LDM/STM 优化）。
     */
    memcpy(buf, (const void *)address, size);
    return 0;
}

void BSP_FLASH_RelocateVectorTable(uint32_t offset)
{
    /*
     * 向量表偏移必须对齐到 512 字节（0x200）。
     * AT32F403A 中 VTOR 的 bit[8:0] 保留，
     * 实际对齐由 NVIC 硬件保证。
     */
    BSP_FLASH_VTOR_SET(offset);
}

void BSP_FLASH_SystemReset(void)
{
    BSP_FLASH_SYSTEM_RESET();
}

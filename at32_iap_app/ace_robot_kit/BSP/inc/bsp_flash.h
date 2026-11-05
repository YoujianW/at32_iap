/***************************************************************
 * bsp_flash.h
 * Flash 通用接口（平台无关）
 ***************************************************************/
#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdint.h>
#include <stddef.h>

/* Flash 句柄 */
typedef struct {
    uint32_t base_address;   /* 操作基地址                       */
    uint32_t sector_size;    /* Sector 大小（字节）              */
} BSP_FLASH_Handle_t;

/**
 * @brief 初始化 Flash 句柄
 * @param handle       句柄指针
 * @param base_address Flash 操作基地址
 */
void BSP_FLASH_Init(BSP_FLASH_Handle_t *handle, uint32_t base_address);

/**
 * @brief 擦除指定地址范围的 Flash sector
 *
 * 会自动对齐到 sector 边界，擦除涵盖 [addr, addr+size) 的所有 sector。
 * 超出内部 Flash 范围或低于句柄基地址时返回 -1。
 *
 * @param handle    句柄指针
 * @param address   起始地址（自动对齐到 sector 起始）
 * @param size      字节大小
 * @return          0=成功, -1=失败
 */
int BSP_FLASH_Erase(const BSP_FLASH_Handle_t *handle,
                    uint32_t address, size_t size);

/**
 * @brief 写入数据到 Flash（32bit 字对齐写入）
 *
 * 注意：写入前必须先擦除对应 sector（Flash 只能 1→0，擦除恢复为 0xFF）
 *
 * @param handle    句柄指针
 * @param address   目标地址（必须 4 字节对齐）
 * @param data      源数据指针
 * @param size      字节大小（必须是 4 的倍数）
 * @return          0=成功, -1=失败
 */
int BSP_FLASH_Write(const BSP_FLASH_Handle_t *handle,
                    uint32_t address, const uint8_t *data, size_t size);

/**
 * @brief 读取 Flash 内容
 *
 * Flash 可直接通过地址寻址，此函数提供安全边界的封装读取。
 *
 * @param handle    句柄指针
 * @param address   源地址
 * @param buf       目标缓冲区
 * @param size      读取字节数
 * @return          0=成功, -1=失败
 */
int BSP_FLASH_Read(const BSP_FLASH_Handle_t *handle,
                   uint32_t address, uint8_t *buf, size_t size);

/**
 * @brief 重定位向量表到指定偏移
 * @param offset    偏移地址（相对于 Flash 基地址 0x08000000）
 */
void BSP_FLASH_RelocateVectorTable(uint32_t offset);

/**
 * @brief 系统复位
 */
void BSP_FLASH_SystemReset(void);

#endif /* BSP_FLASH_H */

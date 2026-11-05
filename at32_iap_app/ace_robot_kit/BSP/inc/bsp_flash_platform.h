/***************************************************************
 * bsp_flash_platform.h
 * Flash 平台适配宏 — 所有 AT32 相关操作集中于此
 ***************************************************************/
#ifndef BSP_FLASH_PLATFORM_H
#define BSP_FLASH_PLATFORM_H

#include "at32f403a_407_conf.h"

/*================================================================
 * Flash 地址空间（AT32F403AVGT7）
 *================================================================*/

/* Bank1: 0x08000000 ~ 0x0807FFFF（512KB）*/
#define BSP_FLASH_BASE              0x08000000U
#define BSP_FLASH_BANK1_SIZE        0x00080000U   /* 512KB         */

/* Bank2: 0x08080000 ~ 0x080FFFFF（512KB）*/
#define BSP_FLASH_BANK2_SIZE        0x00080000U
#define BSP_FLASH_TOTAL_SIZE        (BSP_FLASH_BANK1_SIZE + BSP_FLASH_BANK2_SIZE)

/* Sector（页）大小：AT32F403A 为 2KB */
#define BSP_FLASH_SECTOR_SIZE       0x00000800U   /* 2KB          */
#define BSP_FLASH_SECTOR_MASK       (~(BSP_FLASH_SECTOR_SIZE - 1))

/* 编程最小单位 */
#define BSP_FLASH_PROG_UNIT         4             /* 32-bit word  */

/* 将地址对齐到 sector 边界 */
#define BSP_FLASH_SECTOR_ALIGN(addr)  \
    ((addr) & BSP_FLASH_SECTOR_MASK)

/* 计算地址所在的 sector 起始地址 */
#define BSP_FLASH_SECTOR_BASE(addr)  \
    BSP_FLASH_SECTOR_ALIGN(addr)

/* 计算 sector 数量 */
#define BSP_FLASH_SECTOR_COUNT(size)  \
    (((size) + BSP_FLASH_SECTOR_SIZE - 1) / BSP_FLASH_SECTOR_SIZE)

/*================================================================
 * Flash 操作封装（直接映射 AT32 HAL）
 *================================================================*/

#define BSP_FLASH_UNLOCK()          flash_unlock()
#define BSP_FLASH_LOCK()            flash_lock()

/*
 * sector_address 必须是 sector 对齐的地址（如 0x08000000, 0x08000800）
 * 每次只能擦除一个 sector，内部自动等待完成
 */
#define BSP_FLASH_SECTOR_ERASE(sector_addr)  \
    flash_sector_erase((uint32_t)(sector_addr))

#define BSP_FLASH_WORD_PROGRAM(addr, data)   \
    flash_word_program((uint32_t)(addr), (uint32_t)(data))

/*================================================================
 * 向量表重定位
 *================================================================*/
#define BSP_FLASH_VTOR_SET(offset)  \
    nvic_vector_table_set(NVIC_VECTTAB_FLASH, (uint32_t)(offset))

/*================================================================
 * 系统复位
 *================================================================*/
#define BSP_FLASH_SYSTEM_RESET()    nvic_system_reset()

#endif /* BSP_FLASH_PLATFORM_H */

/* add user code begin Header */
/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
  **************************************************************************
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "at32f403a_407_wk_config.h"
#include "wk_debug.h"
#include "wk_usart.h"
#include "wk_gpio.h"
#include "wk_system.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_flash.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */
/*
 * Flash 分区表（总容量 1 MB，1 KB = 1024 字节）
 * --------------------------------------------------------------------------
 * 区域                    起始地址       结束地址（含）   大小
 * --------------------------------------------------------------------------
 * Bootloader 代码区       0x08000000     0x080057FF       0x5800   /  22 KB
 * 升级信息页              0x08005800     0x08005FFF       0x0800   /   2 KB
 * APP 执行区              0x08006000     0x0805FFFF       0x5A000  / 360 KB
 * OTA 暂存区及剩余空间    0x08060000     0x080FFFFF       0xA0000  / 640 KB
 * --------------------------------------------------------------------------
 * 升级标志：0x08005800，值为 0xAA5555AA 时执行升级。
 * 固件长度：0x08005810，单位为字节。
 * APP 和升级固件最大为 360 KB；本工程尚未单独划分参数区。
 */
#define APP_UPDATE_FLAG_ADDR  0x08005800U
#define APP_UPDATE_FLAG       0xAA5555AAU
#define TOTAL_LENGTH_ADDR     0x08005810U
#define APP_START_ADDR        0x08006000U
#define APP_OTA_DATA_ADDR     0x08060000U
#define PAGE_SIZE             0x800U
/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */
static BSP_FLASH_Handle_t flash_handle;
static uint32_t ota_page_buffer[PAGE_SIZE / sizeof(uint32_t)];
/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */
/* 通过 RAM 缓冲区逐页搬运 Flash 数据。 */
static void bootloader_copy_ota_data(uint32_t page_count)
{
  uint32_t page;
  uint32_t source;
  uint32_t destination;

  for(page = 0; page < page_count; page++)
  {
    source = APP_OTA_DATA_ADDR + page * PAGE_SIZE;
    destination = APP_START_ADDR + page * PAGE_SIZE;

    BSP_FLASH_Read(&flash_handle, source, (uint8_t *)ota_page_buffer, PAGE_SIZE); // 读取暂存页
    BSP_FLASH_Erase(&flash_handle, destination, PAGE_SIZE);                       // 擦除 APP 目标页
    BSP_FLASH_Write(&flash_handle, destination, (const uint8_t *)ota_page_buffer, PAGE_SIZE); // 写入 APP 目标页
  }
}

static void bootloader_jump_to_app(void)
{
  static void (*jump_to_app)(void);                             // APP 复位入口指针
  uint32_t app_stack;
  uint32_t app_entry;

  BSP_FLASH_Read(&flash_handle, APP_START_ADDR, (uint8_t *)&app_stack, sizeof(app_stack)); // 读取 APP 初始栈顶
  BSP_FLASH_Read(&flash_handle, APP_START_ADDR + 4U, (uint8_t *)&app_entry, sizeof(app_entry)); // 读取 APP 复位入口
  jump_to_app = (void (*)(void))app_entry;                       // 设置 APP 入口指针

  __disable_irq();                                            // 屏蔽中断
  SysTick->CTRL = 0U;                                          // 停止 SysTick
  SysTick->LOAD = 0U;                                          // 清空重装载值
  SysTick->VAL = 0U;                                           // 清空当前计数值
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;    // 清除 SysTick 和 PendSV 挂起
  BSP_FLASH_RelocateVectorTable(APP_START_ADDR - FLASH_BASE);   // 切换到 APP 向量表
  __DSB();                                                    // 等待前面的内存访问完成
  __ISB();                                                    // 刷新指令流水线
  __set_MSP(app_stack);                                        // 设置 APP 主栈指针
  __enable_irq();                                             // 解除中断屏蔽
  jump_to_app();                                               // 跳转 APP 复位入口
}
/* add user code end 0 */

/**
  * @brief main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  /* add user code begin 1 */
  uint32_t update_flag;
  uint32_t total_length;
  uint32_t page_count;
  uint32_t page;
  /* add user code end 1 */

  /* system clock config. */
  wk_system_clock_config();

  /* config periph clock. */
  wk_periph_clock_config();

  /* init debug function. */
  wk_debug_config();

  /* nvic config. */
  wk_nvic_config();

  /* timebase config for
     void wk_delay_ms(uint32_t delay); */
  wk_timebase_init();

  /* init gpio function. */
  wk_gpio_config();

  /* usart1 already supports printf. */
  /* init usart1 function. */
  wk_usart1_init();

  /* add user code begin 2 */
  BSP_FLASH_Init(&flash_handle, FLASH_BASE);                    // 初始化 Flash 句柄
  BSP_FLASH_Read(&flash_handle, APP_UPDATE_FLAG_ADDR, (uint8_t *)&update_flag, sizeof(update_flag)); // 读取升级标志
  if(update_flag == APP_UPDATE_FLAG)
  {
    BSP_FLASH_Read(&flash_handle, TOTAL_LENGTH_ADDR, (uint8_t *)&total_length, sizeof(total_length)); // 读取固件长度
    page_count = (total_length + PAGE_SIZE - 1U) / PAGE_SIZE;

    bootloader_copy_ota_data(page_count);

    /* 先清除升级标志，再擦除已使用的暂存页。 */
    BSP_FLASH_Erase(&flash_handle, APP_UPDATE_FLAG_ADDR, PAGE_SIZE);
    for(page = 0; page < page_count; page++)
      BSP_FLASH_Erase(&flash_handle, APP_OTA_DATA_ADDR + page * PAGE_SIZE, PAGE_SIZE);
  }

  bootloader_jump_to_app();
  /* add user code end 2 */

  while(1)
  {
    /* add user code begin 3 */

    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */

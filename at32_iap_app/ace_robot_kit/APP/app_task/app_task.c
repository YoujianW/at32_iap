#include "app_task.h"
#include "app_config.h"

// 如果堆栈空间足够，建议给到512，否则有时串口打印浮点数会卡死
TaskHandle_t Debug_Task_handle;
TaskHandle_t Update_FW_Task_handle;

void app_task_create(void)
{
  xTaskCreate(Debug_Task_func,"Debug_Task",128,NULL,0,&Debug_Task_handle);
  xTaskCreate(Update_FW_Task_func,"app_update_fw_task",256,NULL,0,&Update_FW_Task_handle);
}

void app_freertos_init(void)
{
  taskENTER_CRITICAL();

  app_task_create();

  taskEXIT_CRITICAL();
  vTaskStartScheduler();
}

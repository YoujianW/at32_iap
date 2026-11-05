#include "app_task.h"
#include "led.h"

void Debug_Task_func(void *pvParameters)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        LED0_TOGGLE();
    }
}

#ifndef __APP_TASK_H
#define __APP_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wk_system.h"
#include "at32f403a_407_int.h"
#include "FreeRTOSConfig.h"
#include "freertos_app.h"
#include "app_debug_task.h"

void app_task_create(void);
void app_freertos_init(void);

#ifdef __cplusplus
}
#endif

#endif

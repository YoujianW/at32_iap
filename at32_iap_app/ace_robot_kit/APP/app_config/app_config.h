#ifndef ACE_APP_CONFIG_H
#define ACE_APP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 在芯片外设全部初始化完成后调用此函数。启动 FreeRTOS。 */
void app_config_init(void);

#ifdef __cplusplus
}
#endif

#endif

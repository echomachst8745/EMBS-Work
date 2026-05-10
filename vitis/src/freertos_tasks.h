// FreeRTOS tasks

#ifndef FREERTOS_TASKS_H_
#define FREERTOS_TASKS_H_

#ifdef __cplusplus
extern "C" {
#endif

// Called by network.c once DHCP finishes
void ApplicationTask();

#ifdef __cplusplus
}
#endif

#endif

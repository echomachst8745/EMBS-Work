#include <stdio.h>
#include <string.h>

#include "xparameters.h"
#include "xil_printf.h"
#include "xil_cache.h"

#include "FreeRTOS.h"
#include "task.h"

#include "network.h"
#include "graphics.h"
#include "hls_accel_driver.h"
#include "freertos_tasks.h"
#include "config.h"

static unsigned char networkMacAddress[6] = {
    0x00, 0x11, 0x22, 0x33, 0x00, EMBS_MAC_USER_BYTE
};

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    if (!HLSInit())
    {
        xil_printf("HLS init failed\r\n");
    }
    if (!GraphicsInit())
    {
        xil_printf("Graphics init failed\r\n");
    }

    NetworkInit(networkMacAddress, ApplicationTask);
    vTaskStartScheduler();

    Xil_DCacheDisable();
    Xil_ICacheDisable();

    return 0;
}

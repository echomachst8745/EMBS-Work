// Network startup

#include "network.h"

#include <stddef.h>

#include "xparameters.h"
#include "xil_printf.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/dhcp.h"
#include "lwip/ip.h"
#include "netif/xadapter.h"

void lwip_init(void);

// Stack size for network startup tasks
#define THREAD_STACKSIZE 1024

// lwIP network interface and application callback shared by the startup tasks
static struct netif networkInterface;
static unsigned char *globalMacAddress;
static void (*pGlobalApplicationTask)();

// Task handles for FreeRTOS
static TaskHandle_t startupTaskHandle;
static TaskHandle_t networkTaskHandle;
static TaskHandle_t applicationTaskHandle;
static TaskHandle_t receiveTaskHandle;

static void NetworkThread();
static void NetworkStartup();

void NetworkInit(unsigned char *macAddress, void (*pApplicationTask)())
{
    globalMacAddress = macAddress;
    pGlobalApplicationTask = pApplicationTask;
    
    xTaskCreate((TaskFunction_t)NetworkStartup, "startup",
                THREAD_STACKSIZE, NULL, DEFAULT_THREAD_PRIO, &startupTaskHandle);
}

static void NetworkStartup()
{
    lwip_init();

    xTaskCreate(NetworkThread, "lwip", THREAD_STACKSIZE, NULL,
                DEFAULT_THREAD_PRIO, &networkTaskHandle);

    while (1)
    {
        vTaskDelay(DHCP_FINE_TIMER_MSECS / portTICK_RATE_MS);

        if (networkInterface.ip_addr.addr)
        {
            const ip_addr_t *ip = &networkInterface.ip_addr;

            // DHCP has assigned an IP address, so the application can start
            xil_printf("IP: %d.%d.%d.%d\r\n",
                       ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));

            xTaskCreate((TaskFunction_t)pGlobalApplicationTask, "application",
                        THREAD_STACKSIZE * 2, NULL, DEFAULT_THREAD_PRIO,
                        &applicationTaskHandle);
            break;
        }
    }

    vTaskDelete(NULL);
}

static void NetworkThread(void *pTask)
{
    (void)pTask;
    
    struct netif *pNetIF = &networkInterface;
    ip_addr_t ipAddress = {0}, netmask = {0}, gateway = {0};
    int timeCountMs = 0;

    if (!xemac_add(pNetIF, &ipAddress, &netmask, &gateway, globalMacAddress, XPAR_XEMACPS_0_BASEADDR))
    {
        xil_printf("xemac_add failed\r\n");
        return;
    }

    netif_set_default(pNetIF);
    netif_set_up(pNetIF);

    // Receive thread passes incoming Ethernet packets into lwIP
    xTaskCreate((TaskFunction_t)xemacif_input_thread, "rx",
                THREAD_STACKSIZE, pNetIF, DEFAULT_THREAD_PRIO, &receiveTaskHandle);

    dhcp_start(pNetIF);
    while (1)
    {
        vTaskDelay(DHCP_FINE_TIMER_MSECS / portTICK_RATE_MS);

        dhcp_fine_tmr();

        timeCountMs += DHCP_FINE_TIMER_MSECS;

        if (timeCountMs >= DHCP_COARSE_TIMER_SECS * 1000)
        {
            dhcp_coarse_tmr();
            timeCountMs = 0;
        }
    }
}

// Network startup

#ifndef NETWORK_H_
#define NETWORK_H_

#include "lwip/ip.h"
#include "netif/xadapter.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts network initialisation and launches the application task after DHCP finishes
void NetworkInit(unsigned char *macAddress, void (*pApplicationTask)());

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file    lwip.c
 * @brief   lwIP middleware initialization (CubeMX-style, NO_SYS polling mode).
 *
 * This file is largely CubeMX-generated and provides:
 *  - MX_LWIP_Init(): lwIP stack init + netif setup (static IPv4)
 *  - MX_LWIP_Process(): polling hook to feed RX packets + run lwIP timeouts
 *  - Link management: periodic link check + optional link status callback
 *
 * Project-specific notes:
 *  - DHCP is intentionally disabled (static IP configuration is used).
 *  - MX_LWIP_Process() is intended to be called regularly from the main loop
 *    in NO_SYS configurations (no RTOS).
 */

#include "lwip.h"
#include "lwip/init.h"
#include "lwip/netif.h"

#if defined ( __CC_ARM )  /* MDK ARM Compiler */
#include "lwip/sio.h"
#endif

#include "ethernetif.h"

/* =============================================================================
 * Private function prototypes
 * ============================================================================= */
static void ethernet_link_status_updated(struct netif *netif);
static void Ethernet_Link_Periodic_Handle(struct netif *netif);

/* Provided elsewhere (CubeMX typically declares it globally) */
void Error_Handler(void);

/* DHCP variables (kept from CubeMX template; unused when DHCP is disabled) */
uint32_t DHCPfineTimer   = 0;
uint32_t DHCPcoarseTimer = 0;

/* Link polling timestamp */
uint32_t EthernetLinkTimer;

/* =============================================================================
 * Global network interface and IP configuration
 * ============================================================================= */
struct netif gnetif;

ip4_addr_t ipaddr;
ip4_addr_t netmask;
ip4_addr_t gw;

/**
 * @brief Initialize lwIP stack and configure the default network interface.
 *
 * Current configuration:
 *  - Static IPv4:
 *      IP      = 192.168.1.51
 *      Netmask = 255.255.255.0
 *      Gateway = 0.0.0.0 (no gateway)
 *  - Adds Ethernet netif using ethernetif_init()
 *  - Sets netif default, brings it up, and installs link callback
 *
 * If you want DHCP, enable dhcp_start(&gnetif) and set the IPs to 0.0.0.0.
 */
void MX_LWIP_Init(void)
{
  /* Initialize lwIP core */
  lwip_init();

  /* Static IP configuration (adjust to your network) */
  IP_ADDR4(&ipaddr,  192, 168, 1, 51);
  IP_ADDR4(&netmask, 255, 255, 255, 0);
  IP_ADDR4(&gw,      0,   0,   0,   0);

  /* Add and configure Ethernet network interface */
  netif_add(&gnetif,
            &ipaddr, &netmask, &gw,
            NULL,
            &ethernetif_init,
            &ethernet_input);

  netif_set_default(&gnetif);
  netif_set_up(&gnetif);

  /* Link status callback (called when link changes) */
  netif_set_link_callback(&gnetif, ethernet_link_status_updated);

  /* DHCP disabled by design in this project */
  /* dhcp_start(&gnetif); */
}

/**
 * @brief Periodically checks Ethernet link state (polling).
 *
 * CubeMX approach:
 *  - Call ethernet_link_check_state() every 100ms.
 *  - This keeps netif link status up to date in NO_SYS setups.
 */
static void Ethernet_Link_Periodic_Handle(struct netif *netif)
{
  /* Ethernet link check every 100ms */
  if (HAL_GetTick() - EthernetLinkTimer >= 100)
  {
    EthernetLinkTimer = HAL_GetTick();
    ethernet_link_check_state(netif);
  }
}

/**
 * @brief lwIP processing hook for NO_SYS mode.
 *
 * Responsibilities:
 *  - Feed received Ethernet frames into lwIP (ethernetif_input)
 *  - Run lwIP timeouts (sys_check_timeouts)
 *  - Periodically update link status (Ethernet_Link_Periodic_Handle)
 *
 * This function must be called regularly from the main loop.
 */
void MX_LWIP_Process(void)
{
  /* Read packets from ETH DMA and pass them to lwIP */
  ethernetif_input(&gnetif);

  /* Handle lwIP timers (ARP, TCP, DHCP if enabled, etc.) */
  sys_check_timeouts();

  /* Keep netif link state in sync */
  Ethernet_Link_Periodic_Handle(&gnetif);
}

/**
 * @brief Link status callback (optional user hook).
 *
 * Called by lwIP when netif link changes and LWIP_NETIF_LINK_CALLBACK is enabled.
 * You may add user feedback here (LED, log message, UI line, etc.).
 */
static void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_up(netif))
  {
    /* Link is up */
    /* USER CODE: e.g., log or LED indication */
  }
  else
  {
    /* Link is down */
    /* USER CODE: e.g., log or LED indication */
  }
}

#if defined ( __CC_ARM )  /* MDK ARM Compiler */
/* =============================================================================
 * Serial I/O stubs (CubeMX template)
 * =============================================================================
 * These are placeholders used by certain lwIP options. If you do not use
 * lwIP serial I/O, they can remain as stubs.
 */

sio_fd_t sio_open(u8_t devnum)
{
  (void)devnum;
  sio_fd_t sd;

  /* Dummy implementation */
  sd = 0;
  return sd;
}

void sio_send(u8_t c, sio_fd_t fd)
{
  (void)c;
  (void)fd;
  /* Dummy implementation */
}

u32_t sio_read(sio_fd_t fd, u8_t *data, u32_t len)
{
  (void)fd;
  (void)data;
  (void)len;

  /* Dummy implementation */
  return 0;
}

u32_t sio_tryread(sio_fd_t fd, u8_t *data, u32_t len)
{
  (void)fd;
  (void)data;
  (void)len;

  /* Dummy implementation */
  return 0;
}
#endif /* __CC_ARM */

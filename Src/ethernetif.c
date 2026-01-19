/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : ethernetif.c
  * Description        : This file provides the network interface for LwIP
  *                      (glue between LwIP and ETH HAL).
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "eth.h"              /* <-- IMPORTANT: uses CubeMX eth.c globals (heth, TxConfig) */
#include "ethernetif.h"
#include "lan8742.h"

#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/timeouts.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"

#include <string.h>

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* Private define ------------------------------------------------------------*/
#define IFNAME0 's'
#define IFNAME1 't'

#define ETH_DMA_TRANSMIT_TIMEOUT  (20U)

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

/* Private types -------------------------------------------------------------*/
typedef enum
{
  RX_ALLOC_OK    = 0x00,
  RX_ALLOC_ERROR = 0x01
} RxAllocStatusTypeDef;

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31] __ALIGNED(32);
} RxBuff_t;

/* Memory Pool Declaration */
#define ETH_RX_BUFFER_CNT  12U
LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

/* Private variables ---------------------------------------------------------*/
static uint8_t RxAllocStatus;

/* IMPORTANT:
   heth and TxConfig are defined in CubeMX Src/eth.c.
   We only declare them here. */
extern ETH_HandleTypeDef heth;
extern ETH_TxPacketConfig TxConfig;

/* PHY objects */
lan8742_Object_t LAN8742;
lan8742_IOCtx_t  LAN8742_IOCtx;

/* Private function prototypes -----------------------------------------------*/
static void low_level_init(struct netif *netif);
static err_t low_level_output(struct netif *netif, struct pbuf *p);
static struct pbuf *low_level_input(struct netif *netif);

void pbuf_free_custom(struct pbuf *p);

int32_t ETH_PHY_IO_Init(void);
int32_t ETH_PHY_IO_DeInit(void);
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t ETH_PHY_IO_GetTick(void);

/* USER CODE BEGIN 2 */
/* USER CODE END 2 */

ETH_TxPacketConfig TxConfig;


/*******************************************************************************
                       LL Driver Interface ( LwIP stack --> ETH)
*******************************************************************************/
static void low_level_init(struct netif *netif)
{
  /* ETH is initialized in MX_ETH_Init() (Src/eth.c). Do NOT re-init here. */

  /* Initialize the RX POOL */
  LWIP_MEMPOOL_INIT(RX_POOL);

#if LWIP_ARP || LWIP_ETHERNET
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* MAC address from heth.Init.MACAddr (already set in MX_ETH_Init) */
  netif->hwaddr[0] = heth.Init.MACAddr[0];
  netif->hwaddr[1] = heth.Init.MACAddr[1];
  netif->hwaddr[2] = heth.Init.MACAddr[2];
  netif->hwaddr[3] = heth.Init.MACAddr[3];
  netif->hwaddr[4] = heth.Init.MACAddr[4];
  netif->hwaddr[5] = heth.Init.MACAddr[5];

  netif->mtu = ETH_MAX_PAYLOAD;

#if LWIP_ARP
  netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
#else
  netif->flags |= NETIF_FLAG_BROADCAST;
#endif

  /* Set PHY IO functions */
  LAN8742_IOCtx.Init     = ETH_PHY_IO_Init;
  LAN8742_IOCtx.DeInit   = ETH_PHY_IO_DeInit;
  LAN8742_IOCtx.WriteReg = ETH_PHY_IO_WriteReg;
  LAN8742_IOCtx.ReadReg  = ETH_PHY_IO_ReadReg;
  LAN8742_IOCtx.GetTick  = ETH_PHY_IO_GetTick;

  LAN8742_RegisterBusIO(&LAN8742, &LAN8742_IOCtx);

  if (LAN8742_Init(&LAN8742) != LAN8742_STATUS_OK)
  {
    netif_set_link_down(netif);
    netif_set_down(netif);
    return;
  }

  /* Get link state and start MAC accordingly */
  ethernet_link_check_state(netif);
#endif
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  (void)netif;

  uint32_t i = 0U;
  struct pbuf *q = NULL;
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT] = {0};

  for (q = p; q != NULL; q = q->next)
  {
    if (i >= ETH_TX_DESC_CNT)
      return ERR_IF;

    Txbuffer[i].buffer = q->payload;
    Txbuffer[i].len    = q->len;

    if (i > 0)
      Txbuffer[i - 1].next = &Txbuffer[i];

    if (q->next == NULL)
      Txbuffer[i].next = NULL;

    i++;
  }

  TxConfig.Length   = p->tot_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData    = p;

  if (HAL_ETH_Transmit(&heth, &TxConfig, ETH_DMA_TRANSMIT_TIMEOUT) != HAL_OK)
    return ERR_IF;

  return ERR_OK;
}

static struct pbuf *low_level_input(struct netif *netif)
{
  (void)netif;

  struct pbuf *p = NULL;

  if (RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_ETH_ReadData(&heth, (void **)&p);
  }

  return p;
}

void ethernetif_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  do
  {
    p = low_level_input(netif);
    if (p != NULL)
    {
      if (netif->input(p, netif) != ERR_OK)
      {
        pbuf_free(p);
      }
    }
  } while (p != NULL);
}

err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  netif->hostname = "lwip";
#endif

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;

#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
  netif->output = etharp_output;
#else
  /* if ARP off, user must implement their own output */
  netif->output = NULL;
#endif
#endif
#endif

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif

  netif->linkoutput = low_level_output;

  low_level_init(netif);

  return ERR_OK;
}

void pbuf_free_custom(struct pbuf *p)
{
  struct pbuf_custom* custom_pbuf = (struct pbuf_custom*)p;
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  if (RxAllocStatus == RX_ALLOC_ERROR)
    RxAllocStatus = RX_ALLOC_OK;
}

/* USER CODE BEGIN 6 */
u32_t sys_now(void)
{
  return HAL_GetTick();
}
/* USER CODE END 6 */

/*******************************************************************************
                       PHY IO Functions
*******************************************************************************/
int32_t ETH_PHY_IO_Init(void)
{
  HAL_ETH_SetMDIOClockRange(&heth);
  return 0;
}

int32_t ETH_PHY_IO_DeInit(void)
{
  return 0;
}

int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  return (HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) == HAL_OK) ? 0 : -1;
}

int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  return (HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) == HAL_OK) ? 0 : -1;
}

int32_t ETH_PHY_IO_GetTick(void)
{
  return HAL_GetTick();
}

void ethernet_link_check_state(struct netif *netif)
{
  ETH_MACConfigTypeDef MACConf = {0};
  int32_t PHYLinkState = 0;
  uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;

  PHYLinkState = LAN8742_GetLinkState(&LAN8742);

  if (netif_is_link_up(netif) && (PHYLinkState <= LAN8742_STATUS_LINK_DOWN))
  {
    HAL_ETH_Stop(&heth);
    netif_set_down(netif);
    netif_set_link_down(netif);
  }
  else if (!netif_is_link_up(netif) && (PHYLinkState > LAN8742_STATUS_LINK_DOWN))
  {
    switch (PHYLinkState)
    {
      case LAN8742_STATUS_100MBITS_FULLDUPLEX: duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_100M; linkchanged = 1; break;
      case LAN8742_STATUS_100MBITS_HALFDUPLEX: duplex = ETH_HALFDUPLEX_MODE; speed = ETH_SPEED_100M; linkchanged = 1; break;
      case LAN8742_STATUS_10MBITS_FULLDUPLEX:  duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_10M;  linkchanged = 1; break;
      case LAN8742_STATUS_10MBITS_HALFDUPLEX:  duplex = ETH_HALFDUPLEX_MODE; speed = ETH_SPEED_10M;  linkchanged = 1; break;
      default: break;
    }

    if (linkchanged)
    {
      HAL_ETH_GetMACConfig(&heth, &MACConf);
      MACConf.DuplexMode = duplex;
      MACConf.Speed = speed;
      HAL_ETH_SetMACConfig(&heth, &MACConf);

      HAL_ETH_Start(&heth);
      netif_set_up(netif);
      netif_set_link_up(netif);
    }
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p)
  {
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
    p->custom_free_function = pbuf_free_custom;
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUF_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  if (!*ppStart)
    *ppStart = p;
  else
    (*ppEnd)->next = p;

  *ppEnd = p;

  for (p = *ppStart; p != NULL; p = p->next)
    p->tot_len += Length;

  SCB_InvalidateDCache_by_Addr((uint32_t *)buff, Length);
}

void HAL_ETH_TxFreeCallback(uint32_t * buff)
{
  pbuf_free((struct pbuf *)buff);
}

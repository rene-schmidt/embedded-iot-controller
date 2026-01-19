/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mx_lwip.h
  * @brief   Header for mx_lwip.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef MX_LWIP_H
#define MX_LWIP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "ethernetif.h"

/* Includes for RTOS ---------------------------------------------------------*/
#if (WITH_RTOS)
#include "lwip/tcpip.h"
#endif /* WITH_RTOS */

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* Exported variables --------------------------------------------------------*/
extern ETH_HandleTypeDef heth;

/* Exported functions prototypes ---------------------------------------------*/
void MX_LWIP_Init(void);

#if !(WITH_RTOS)
void MX_LWIP_Process(void);
#endif /* !WITH_RTOS */

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif

#endif /* MX_LWIP_H */

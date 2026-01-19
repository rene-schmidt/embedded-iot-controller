/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * File Name          : ethernetif.h
  * Description        : This file provides initialization code for LWIP
  *                      middleWare.
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
#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "lwip/err.h"
#include "lwip/netif.h"

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* Exported functions prototypes ---------------------------------------------*/
err_t ethernetif_init(struct netif *netif);

void ethernetif_input(struct netif *netif);
void ethernet_link_check_state(struct netif *netif);

u32_t sys_jiffies(void);
u32_t sys_now(void);

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif

#endif /* ETHERNETIF_H */

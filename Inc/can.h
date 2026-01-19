/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   CAN1 application interface
  ******************************************************************************
  * @attention
  * This file is generated/maintained within the project and may be overwritten.
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
#ifndef CAN1_H
#define CAN1_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "main.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported variables --------------------------------------------------------*/
extern CAN_HandleTypeDef hcan1;

/* USER CODE BEGIN EV */
/* USER CODE END EV */

/* Exported functions prototypes ---------------------------------------------*/
/* init/start */
void MX_CAN1_Init(void);
void CAN1_Start(void);

/* service hook (called by App_CAN_Service()) */
void CAN1_Service(void);

/* text/status API (used by app_helpers.c / CLI / UI) */
const char *CAN1_GetLastText(void);
const char *CAN1_GetText_0x101(void);
const char *CAN1_GetText_0x120(void);

/* structured 0x101 access */
uint8_t CAN1_101_IsValid(void);

/* structured 0x120 access */
uint8_t  CAN1_120_IsValid(void);
uint32_t CAN1_120_GetLux(void);   /* lux (integer, not x100) */
uint16_t CAN1_120_GetFull(void);
uint16_t CAN1_120_GetIR(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* CAN1_H */

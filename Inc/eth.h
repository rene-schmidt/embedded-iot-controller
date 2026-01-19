/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    eth.h
  * @brief   Header for eth.c file
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
#ifndef ETH_H
#define ETH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f7xx_hal.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported variables --------------------------------------------------------*/
extern ETH_HandleTypeDef heth;

/* USER CODE BEGIN EV */
/* USER CODE END EV */

/* Exported functions prototypes ---------------------------------------------*/
void MX_ETH_Init(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* ETH_H */

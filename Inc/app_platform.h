/* USER CODE BEGIN Header */
/******************************************************************************
 * File:    app_platform.h
 * Brief:   Platform-level application interface
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *****************************************************************************/
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef APP_PLATFORM_H
#define APP_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx_hal.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported functions prototypes ---------------------------------------------*/
/* Platform-level functions expected by CubeMX-generated code */
void SystemClock_Config(void);
void MPU_Config(void);
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* APP_PLATFORM_H */

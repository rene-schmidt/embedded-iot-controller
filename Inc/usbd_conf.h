/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_conf.h
  * @version        : v1.0_Cube
  * @brief          : Header for usbd_conf.c file.
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
#ifndef USBD_CONF_H
#define USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
#define USBD_MAX_NUM_INTERFACES        1U
#define USBD_MAX_NUM_CONFIGURATION     1U
#define USBD_MAX_STR_DESC_SIZ          512U
#define USBD_DEBUG_LEVEL               0U
#define USBD_LPM_ENABLED               1U
#define USBD_SELF_POWERED              1U

/* FS and HS identification */
#define DEVICE_FS                      0U
#define DEVICE_HS                      1U

/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* Memory management macros */
#define USBD_malloc                    malloc
#define USBD_free                      free
#define USBD_memset                    memset
#define USBD_memcpy                    memcpy
#define USBD_Delay                     HAL_Delay

/* DEBUG macros */
#if (USBD_DEBUG_LEVEL > 0)
  #define USBD_UsrLog(...) do { printf(__VA_ARGS__); printf("\n"); } while (0)
#else
  #define USBD_UsrLog(...) do { } while (0)
#endif

#if (USBD_DEBUG_LEVEL > 1)
  #define USBD_ErrLog(...) do { printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
  #define USBD_ErrLog(...) do { } while (0)
#endif

#if (USBD_DEBUG_LEVEL > 2)
  #define USBD_DbgLog(...) do { printf("DEBUG : "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
  #define USBD_DbgLog(...) do { } while (0)
#endif

/* USER CODE BEGIN EM */
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* USBD_CONF_H */

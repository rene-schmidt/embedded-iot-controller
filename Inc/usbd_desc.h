/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_desc.h
  * @version        : v1.0_Cube
  * @brief          : Header for usbd_desc.c file.
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
#ifndef __USBD_DESC_H
#define __USBD_DESC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "usbd_def.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
#define DEVICE_ID1               (UID_BASE)
#define DEVICE_ID2               (UID_BASE + 0x4U)
#define DEVICE_ID3               (UID_BASE + 0x8U)

#define USB_SIZ_STRING_SERIAL    0x1AU

/* USER CODE BEGIN EM */
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
extern USBD_DescriptorsTypeDef FS_Desc;

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* __USBD_DESC_H */

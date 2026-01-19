/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.h
  * @version        : v1.0_Cube
  * @brief          : Header for usbd_cdc_if.c file.
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
#ifndef USBD_CDC_IF_H
#define USBD_CDC_IF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* Define size for the receive and transmit buffer over CDC */
#define APP_RX_DATA_SIZE            2048U
#define APP_TX_DATA_SIZE            2048U

/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
/* USER CODE END EM */

/* Exported variables --------------------------------------------------------*/
extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

/* USER CODE BEGIN EV */
/* USER CODE END EV */

/* Exported functions prototypes ---------------------------------------------*/
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

uint8_t CDC_ReadLine(char *out, uint16_t out_sz);
void    CDC_ConsoleTxService(void);
void    CDC_ConsolePrintSafe(const char *s);
uint8_t CDC_ConsoleTxIsEmpty(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* USBD_CDC_IF_H */

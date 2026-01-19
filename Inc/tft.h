/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tft_driver.h
  * @brief   TFT driver public API
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
#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "stm32f7xx_hal.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported constants --------------------------------------------------------*/
/* Display geometry (orientation handled in tft.c) */
#define TFT_WIDTH                   160U
#define TFT_HEIGHT                  128U

/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/* Core API */
void TFT_Init(void);

/* Non-blocking fill engine */
void    TFT_FillColor_Async(uint16_t color565);
void    TFT_Task(void);
uint8_t TFT_IsBusy(void);

/* Text drawing */
void TFT_DrawTextLine_Async(uint16_t y,
                            const char *text,
                            uint16_t fg565,
                            uint16_t bg565);

/* Optional demo */
void TFT_RGB_Cycle_Start(uint32_t hold_ms);
void TFT_RGB_Cycle_Stop(void);
void TFT_RGB_Cycle_Task(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* TFT_DRIVER_H */

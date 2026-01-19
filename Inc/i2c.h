/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c_app.h
  * @brief   I2C application interface (ESP32 temperature)
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
#ifndef I2C_APP_H
#define I2C_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "stm32f7xx_hal.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported constants --------------------------------------------------------*/
#define ESP32_I2C_ADDR_7BIT          (0x28U)

/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported variables --------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN EV */
/* USER CODE END EV */

/* Exported functions prototypes ---------------------------------------------*/
void MX_I2C1_Init(void);

/* Low-level read */
HAL_StatusTypeDef I2C_ReadTempFromESP32(float *temp_celsius_out);

/* Service (called from main loop) */
void App_I2C_Service(uint32_t now_ms);

/* UI-compatible getters */
uint8_t     App_I2C_IsOk(void);
int         App_I2C_GetTempInt(void);
const char *App_I2C_GetLastErr(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* I2C_APP_H */

/* USER CODE BEGIN Header */
/******************************************************************************
 * File:    app_helpers.h
 * Brief:   Application helper interfaces
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
#ifndef APP_HELPERS_H
#define APP_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "stm32f7xx_hal.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported functions prototypes ---------------------------------------------*/

/* App lifecycle */
void App_Init(void);
void App_Tick(uint32_t now_ms);

/* Services called from main loop */
void App_USB_Service(void);
void App_CAN_Service(uint32_t now_ms);
void App_I2C_Service(uint32_t now_ms);
void App_TFT_Service(uint32_t now_ms);
void App_CLI_Service(uint32_t now_ms);

/* USB log ring status */
uint8_t App_USBLog_IsEmpty(void);

/* I2C getters for UI */
uint8_t     App_I2C_IsOk(void);
int         App_I2C_GetTempInt(void);
const char *App_I2C_GetLastErr(void);

/* UI line manager */
void App_UI_ClearAll(void);
void App_UI_ClearLine(uint8_t idx);
void App_UI_SetLine(uint8_t idx, uint16_t fg, uint16_t bg, const char *text);
void App_UI_SetLineF(uint8_t idx, uint16_t fg, uint16_t bg,
                     const char *fmt, ...);

/* Optional UI feed hook
   Implemented weakly elsewhere; default does nothing */
void App_UserFeedUI(uint32_t now_ms);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* APP_HELPERS_H */

/* USER CODE BEGIN Header */
/******************************************************************************
 * File:    app_spi.h
 * Brief:   SPI application interface
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
#ifndef APP_SPI_H
#define APP_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported functions prototypes ---------------------------------------------*/
void App_SPI_Init(void);
void App_SPI_Service(uint32_t now_ms);

bool App_SPI_IsIdle(void);

/* Starts a DMA TX/RX transfer (returns false if busy) */
bool App_SPI_StartTxRx(uint8_t *tx, uint8_t *rx, uint16_t len);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* APP_SPI_H */

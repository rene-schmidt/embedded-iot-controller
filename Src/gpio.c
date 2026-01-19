/**
 * @file    gpio.c
 * @brief   GPIO configuration for the project.
 *
 * This file initializes all GPIO ports and pins used in the application.
 * It is mostly CubeMX-generated code with small, explicit additions for
 * the TFT control pins (CS, DC, RST).
 *
 * Notes:
 *  - TFT pins are configured as push-pull outputs with very high speed.
 *  - Default output levels for the TFT are set before pin configuration
 *    to avoid unwanted glitches during startup.
 */

#include "gpio.h"
#include "main.h"   /* Needed for TFT_*_Pin and *_GPIO_Port definitions */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* --------------------------------------------------------------------------
   * Enable GPIO port clocks
   * -------------------------------------------------------------------------- */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* TFT uses GPIOE and GPIOF */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  /* --------------------------------------------------------------------------
   * Default output levels
   * -------------------------------------------------------------------------- */

  /* Board LEDs off */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin | LD3_Pin | LD2_Pin, GPIO_PIN_RESET);

  /* USB power switch off */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port,
                    USB_PowerSwitchOn_Pin,
                    GPIO_PIN_RESET);

  /* TFT default state:
   *  - CS  high  (inactive)
   *  - DC  low   (command mode)
   *  - RST high  (not in reset)
   */
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port,  TFT_CS_Pin,  GPIO_PIN_SET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port,  TFT_DC_Pin,  GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);

  /* --------------------------------------------------------------------------
   * User button (external interrupt)
   * -------------------------------------------------------------------------- */
  GPIO_InitStruct.Pin  = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /* --------------------------------------------------------------------------
   * Board LEDs
   * -------------------------------------------------------------------------- */
  GPIO_InitStruct.Pin   = LD1_Pin | LD3_Pin | LD2_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* --------------------------------------------------------------------------
   * USB power switch control
   * -------------------------------------------------------------------------- */
  GPIO_InitStruct.Pin   = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /* USB overcurrent input */
  GPIO_InitStruct.Pin  = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /* --------------------------------------------------------------------------
   * TFT control pins (CS, DC, RST)
   * -------------------------------------------------------------------------- */

  /* TFT CS */
  GPIO_InitStruct.Pin   = TFT_CS_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(TFT_CS_GPIO_Port, &GPIO_InitStruct);

  /* TFT DC */
  GPIO_InitStruct.Pin = TFT_DC_Pin;
  HAL_GPIO_Init(TFT_DC_GPIO_Port, &GPIO_InitStruct);

  /* TFT RST */
  GPIO_InitStruct.Pin = TFT_RST_Pin;
  HAL_GPIO_Init(TFT_RST_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @file    app_platform.c
 * @brief   Platform setup: system clock, MPU/cache configuration, and error handler.
 *
 * This module contains:
 *  - SystemClock_Config(): configures HSE + PLL and bus prescalers
 *  - MPU_Config(): configures MPU regions (cacheable SRAM + optional non-cacheable DMA region)
 *  - Error_Handler(): last-resort error loop with UART message
 *
 * Notes:
 *  - The clock tree parameters must match your board clock source and target frequencies.
 *  - MPU region base addresses and sizes MUST be power-of-two and properly aligned.
 *  - If D-Cache is enabled, DMA buffers must be handled carefully:
 *      * either place them into a non-cacheable region (recommended), or
 *      * perform cache clean/invalidate operations around DMA transfers.
 */

#include "app_platform.h"
#include "usart.h"
#include <string.h>

/* =============================================================================
 * System clock configuration
 * ============================================================================= */

/**
 * @brief Configure system clock using HSE bypass + PLL.
 *
 * Configuration summary (as provided):
 *  - Oscillator: HSE bypass (external clock input, not a crystal)
 *  - PLL source: HSE
 *  - PLLM = 4, PLLN = 96, PLLP = /2, PLLQ = 4, PLLR = 2
 *  - SYSCLK = PLLCLK
 *  - AHB  prescaler = /1
 *  - APB1 prescaler = /2
 *  - APB2 prescaler = /1
 *  - Flash latency = 3
 *
 * Important:
 *  - Exact resulting frequencies depend on your HSE input frequency.
 *  - Adjust FLASH_LATENCY and prescalers if you change SYSCLK.
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* Power configuration (voltage scaling affects max allowed clock) */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /* --- Oscillator + PLL --------------------------------------------------- */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_BYPASS;

  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;

  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  /* --- Bus clocks -------------------------------------------------------- */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK  |
                                RCC_CLOCKTYPE_SYSCLK|
                                RCC_CLOCKTYPE_PCLK1 |
                                RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    Error_Handler();

  /* Update CMSIS SystemCoreClock variable based on current config */
  SystemCoreClockUpdate();
}

/* =============================================================================
 * MPU / Cache configuration
 * ============================================================================= */

/**
 * @brief Configure MPU regions and enable I-Cache/D-Cache (if present).
 *
 * Typical intent:
 *  - Region 0: general SRAM as cacheable for CPU performance
 *  - Region 1: dedicated DMA buffer area as non-cacheable to avoid coherency issues
 *
 * You MUST adapt:
 *  - BaseAddress and Size to match your exact SRAM map and linker placement.
 *  - DMA region must be aligned to its size and not overlap other critical areas.
 */
void MPU_Config(void)
{
#if defined (__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  /* ------------------------------------------------------------------------
   * Region 0: Cacheable SRAM region (example)
   * ------------------------------------------------------------------------
   * Size must be a power-of-two and BaseAddress must be aligned to Size.
   * Adjust to match your MCU SRAM size and layout.
   */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress      = 0x20000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* ------------------------------------------------------------------------
   * Region 1: Non-cacheable DMA buffer region (example)
   * ------------------------------------------------------------------------
   * Place SPI/Ethernet DMA buffers into this region to avoid D-Cache coherency
   * maintenance. The address/size shown below are examples only.
   */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress      = 0x2007C000;           /* Example base address */
  MPU_InitStruct.Size             = MPU_REGION_SIZE_32KB; /* Example size */
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enable MPU with a default privileged map */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

  /* Enable caches (only do this if your memory attributes are correct) */
  SCB_EnableICache();
  SCB_EnableDCache();
#else
  /* If no D-Cache is present, keep MPU in a simple default configuration */
  HAL_MPU_Disable();
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
#endif
}

/* =============================================================================
 * Error handler
 * ============================================================================= */

/**
 * @brief Last-resort error handler.
 *
 * Behavior:
 *  - Emits a short message via UART3 (blocking)
 *  - Enters an infinite loop with a slow delay
 *
 * Notes:
 *  - UART must be initialized before calling Error_Handler() for the message
 *    to be sent successfully.
 *  - In production, you might also toggle an LED, store an error code, or reset.
 */
void Error_Handler(void)
{
  extern UART_HandleTypeDef huart3;

  const char *msg = "ERROR_HANDLER\r\n";
  HAL_UART_Transmit(&huart3, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

  while (1)
  {
    HAL_Delay(1000);
  }
}

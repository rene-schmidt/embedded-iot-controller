/**
 * @file    eth.c
 * @brief   Ethernet (MAC) initialization for STM32 (HAL) using RMII.
 *
 * This module provides:
 *  - ETH peripheral initialization (HAL_ETH_Init)
 *  - DMA descriptor tables for RX/TX
 *  - MSP init: clocks, RMII GPIO alternate functions, ETH IRQ enable
 *
 * Notes:
 *  - The MAC address provided here is a locally administered address (LAA):
 *      0x02 in the first byte sets the "locally administered" bit.
 *    Make sure it is unique in your network.
 *  - Descriptor arrays are 32-byte aligned (required/recommended on STM32F7
 *    due to cache lines and ETH DMA requirements).
 *  - RxBuffLen is set to 1524 bytes (commonly used max packet size incl. overhead).
 */

#include "eth.h"

ETH_HandleTypeDef heth;

/* Locally administered MAC (adjust to your project/network requirements) */
static uint8_t MACAddr[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* =============================================================================
 * DMA descriptor tables
 * =============================================================================
 * ETH DMA reads/writes these descriptors. Alignment is important, especially
 * on cache-enabled MCUs (STM32F7).
 */
#if defined ( __GNUC__ )
ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((aligned(32)));
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((aligned(32)));
#else
/* If using a different compiler, ensure equivalent 32-byte alignment here. */
ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
#endif

/* =============================================================================
 * Public init
 * ============================================================================= */

/**
 * @brief Initialize the Ethernet peripheral in RMII mode.
 *
 * Sets:
 *  - MAC address pointer
 *  - RMII media interface
 *  - TX/RX descriptor table addresses
 *  - RX buffer length (in bytes)
 */
void MX_ETH_Init(void)
{
  heth.Instance = ETH;

  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;

  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;

  /* Typical max frame buffer length for Ethernet (often 1524 bytes) */
  heth.Init.RxBuffLen = 1524;

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }
}

/* =============================================================================
 * MSP init (clocks, pins, IRQ)
 * ============================================================================= */

/**
 * @brief Low-level hardware init for ETH (called by HAL_ETH_Init()).
 *
 * Configures:
 *  - ETH peripheral clock
 *  - GPIO clocks for RMII pins
 *  - GPIO alternate function mappings (AF11_ETH)
 *  - ETH interrupt
 *
 * RMII pin mapping in this configuration (board/MCU dependent):
 *  - GPIOA: PA1, PA2, PA7
 *  - GPIOB: PB13
 *  - GPIOC: PC1, PC4, PC5
 *  - GPIOG: PG11, PG13
 */
void HAL_ETH_MspInit(ETH_HandleTypeDef* ethHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (ethHandle->Instance == ETH)
  {
    /* Enable ETH and required GPIO clocks */
    __HAL_RCC_ETH_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* Common GPIO setup for RMII pins */
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;

    /* PA1, PA2, PA7 */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB13 */
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PC1, PC4, PC5 */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PG11, PG13 */
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* ETH global interrupt (priority depends on your RTOS/stack needs) */
    HAL_NVIC_SetPriority(ETH_IRQn, 12, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
  }
}

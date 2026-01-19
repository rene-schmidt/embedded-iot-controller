/**
 * @file    dma.c
 * @brief   DMA configuration for the project (SPI1 RX/TX on DMA2).
 *
 * This module provides:
 *  - DMA2 clock enable (STM32F7: SPI1 is typically mapped to DMA2)
 *  - DMA stream/channel configuration for SPI1 RX and TX
 *  - NVIC configuration for the corresponding DMA stream interrupts
 *
 * Notes:
 *  - The chosen streams/channels must match the MCU's DMA request mapping and
 *    your CubeMX configuration.
 *  - This file only configures the DMA streams. To actually use DMA with SPI1,
 *    you must link these handles to the SPI handle via __HAL_LINKDMA() in
 *    HAL_SPI_MspInit().
 *  - If you are using cache (D-Cache) on STM32F7, DMA buffers should be placed
 *    carefully and/or cleaned/invalidated as required.
 */

#include "dma.h"

/* SPI1 DMA handles (exported; typically referenced as extern in spi.c) */
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi1_rx;

/**
 * @brief Initialize DMA controller and configure DMA streams used by the project.
 *
 * Configures:
 *  - SPI1_RX: DMA2 Stream2 Channel 3 (Peripheral -> Memory)
 *  - SPI1_TX: DMA2 Stream3 Channel 3 (Memory -> Peripheral)
 *
 * Also enables NVIC interrupts for both DMA streams.
 */
void MX_DMA_Init(void)
{
  /* --------------------------------------------------------------------------
   * Enable DMA controller clock
   * -------------------------------------------------------------------------- */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* --------------------------------------------------------------------------
   * SPI1_RX DMA init: DMA2 Stream2 Channel 3 (PERIPH -> MEM)
   * -------------------------------------------------------------------------- */
  hdma_spi1_rx.Instance                 = DMA2_Stream2;
  hdma_spi1_rx.Init.Channel             = DMA_CHANNEL_3;
  hdma_spi1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;

  hdma_spi1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_spi1_rx.Init.MemInc              = DMA_MINC_ENABLE;

  hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_spi1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;

  hdma_spi1_rx.Init.Mode                = DMA_NORMAL;
  hdma_spi1_rx.Init.Priority            = DMA_PRIORITY_HIGH;

  /* FIFO disabled: direct mode */
  hdma_spi1_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

  if (HAL_DMA_Init(&hdma_spi1_rx) != HAL_OK)
  {
    Error_Handler();
  }

  /* --------------------------------------------------------------------------
   * SPI1_TX DMA init: DMA2 Stream3 Channel 3 (MEM -> PERIPH)
   * -------------------------------------------------------------------------- */
  hdma_spi1_tx.Instance                 = DMA2_Stream3;
  hdma_spi1_tx.Init.Channel             = DMA_CHANNEL_3;
  hdma_spi1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;

  hdma_spi1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_spi1_tx.Init.MemInc              = DMA_MINC_ENABLE;

  hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_spi1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;

  hdma_spi1_tx.Init.Mode                = DMA_NORMAL;
  hdma_spi1_tx.Init.Priority            = DMA_PRIORITY_HIGH;

  /* FIFO disabled: direct mode */
  hdma_spi1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

  if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK)
  {
    Error_Handler();
  }

  /* --------------------------------------------------------------------------
   * DMA interrupts (stream IRQs)
   * --------------------------------------------------------------------------
   * Priority level should be chosen to match your system (bare metal / RTOS).
   * Ensure the IRQ names match the streams above.
   */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

/**
 * @file    app_spi.c
 * @brief   Simple non-blocking SPI1 transfer wrapper using DMA (HAL).
 *
 * This module implements a minimal "job" abstraction:
 *  - Only one TX/RX job at a time (IDLE/BUSY)
 *  - Start transfer via HAL_SPI_TransmitReceive_DMA()
 *  - Completion and error handling via HAL callbacks
 *
 * Intended usage:
 *  - Call App_SPI_Init() once at startup
 *  - When idle, call App_SPI_StartTxRx(tx, rx, len)
 *  - Optionally call App_SPI_Service() from the main loop (currently no-op)
 *
 * Notes:
 *  - DMA must be configured and linked to hspi1 (see MX_DMA_Init() and __HAL_LINKDMA).
 *  - Do not call blocking HAL_SPI_Transmit/Receive functions from this module.
 *  - Callbacks run in IRQ context -> keep them short.
 */

#include "app_spi.h"
#include "spi.h"   /* provides hspi1 and HAL SPI types */

/* =============================================================================
 * Internal state
 * ============================================================================= */

typedef enum {
  SPI_JOB_IDLE = 0,
  SPI_JOB_BUSY
} spi_job_state_t;

static volatile spi_job_state_t s_state = SPI_JOB_IDLE;

/* Last error code captured from the HAL handle (0 = no error) */
static volatile uint32_t s_last_err = 0;

/* =============================================================================
 * Public API
 * ============================================================================= */

/**
 * @brief Initialize the SPI job wrapper state.
 *
 * This does not initialize the SPI peripheral itself (MX_SPI1_Init() does that).
 */
void App_SPI_Init(void)
{
  s_state = SPI_JOB_IDLE;
  s_last_err = 0;
}

/**
 * @brief Returns true if no SPI DMA transfer is currently running.
 */
bool App_SPI_IsIdle(void)
{
  return (s_state == SPI_JOB_IDLE);
}

/**
 * @brief Start a non-blocking SPI full-duplex transfer using DMA.
 *
 * @param tx  TX buffer (must remain valid until callback)
 * @param rx  RX buffer (must remain valid until callback)
 * @param len Number of bytes to transfer (must be > 0)
 *
 * @return true if the transfer was started, false otherwise.
 *
 * Requirements:
 *  - DMA configured and enabled
 *  - DMA handles linked to hspi1 (hdmatx/hdmarx)
 *  - tx/rx buffers must not be on the stack if the caller returns immediately
 */
bool App_SPI_StartTxRx(uint8_t *tx, uint8_t *rx, uint16_t len)
{
  if (s_state != SPI_JOB_IDLE) return false;
  if (len == 0) return false;

  s_state = SPI_JOB_BUSY;

  /* Start DMA transfer (non-blocking) */
  if (HAL_SPI_TransmitReceive_DMA(&hspi1, tx, rx, len) != HAL_OK)
  {
    s_last_err = hspi1.ErrorCode;
    s_state = SPI_JOB_IDLE;
    return false;
  }

  return true;
}

/**
 * @brief Optional periodic service hook.
 *
 * Currently this is a no-op because DMA progress and completion are handled
 * by interrupts/callbacks. This hook is useful if you later add a job queue,
 * timeouts, or retry logic.
 */
void App_SPI_Service(uint32_t now_ms)
{
  (void)now_ms;
}

/* =============================================================================
 * HAL callbacks (IRQ context)
 * ============================================================================= */

/**
 * @brief Called by HAL when the TX/RX DMA transfer completes.
 *        Runs in interrupt context.
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi == &hspi1)
  {
    s_state = SPI_JOB_IDLE;
    /* Optional: signal an event or call an application hook here */
  }
}

/**
 * @brief Called by HAL on SPI error during DMA operation.
 *        Runs in interrupt context.
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi == &hspi1)
  {
    s_last_err = hspi->ErrorCode;
    s_state = SPI_JOB_IDLE;

    /* Optional: perform recovery (abort DMA, re-init peripheral, etc.) */
  }
}

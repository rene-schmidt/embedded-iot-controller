/**
 * @file    can.c
 * @brief   CAN1 initialization and RX snapshot decoding for two standard IDs.
 *
 * This module provides:
 *  - CAN1 MSP init (GPIO + IRQ)
 *  - CAN1 init for a "robust" 500 kbit/s timing (as configured)
 *  - Filter configuration (accept all, FIFO0)
 *  - RX interrupt callback that decodes:
 *      * 0x101: heartbeat sequence byte
 *      * 0x120: light sensor payload (8 bytes, little-endian fields)
 *  - Text getters for UI and structured getters for app logic
 *
 * Design notes:
 *  - ISR (RX callback) updates "snapshots" and formatted text buffers.
 *  - Getter functions implement a simple freshness timeout (2 seconds).
 *  - No TX is implemented here; only RX is handled.
 */

#include "can.h"
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * Global handle
 * ============================================================================= */
CAN_HandleTypeDef hcan1;

/* =============================================================================
 * Snapshot state (updated from IRQ)
 * ============================================================================= */
static volatile uint8_t  s_has101   = 0;
static volatile uint8_t  s_has120   = 0;

static volatile uint8_t  s_hb_seq   = 0;
static volatile uint32_t s_lux_x100 = 0; /* lux value scaled by 100 */
static volatile uint16_t s_full     = 0;
static volatile uint16_t s_ir       = 0;

/* =============================================================================
 * Last text buffers + timestamps (used by UI getters)
 * ============================================================================= */
static char     s_last_txt[128] = "no data";
static char     s_101_txt[128]  = "none";
static char     s_120_txt[128]  = "none";
static uint32_t s_last_tick     = 0;
static uint32_t s_101_tick      = 0;
static uint32_t s_120_tick      = 0;

/* =============================================================================
 * Little-endian helpers (payload decoding)
 * ============================================================================= */
static inline uint32_t u32_le(const uint8_t *p)
{
  return ((uint32_t)p[0])       |
         ((uint32_t)p[1] << 8)  |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static inline uint16_t u16_le(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* =============================================================================
 * MSP init: CAN1 pins + IRQ
 * ============================================================================= */

/**
 * @brief Low-level hardware init for CAN1 (called by HAL_CAN_Init()).
 *
 * Pin mapping (board/MCU dependent):
 *  - PD0 / PD1 configured as AF9 CAN1
 *
 * Interrupt:
 *  - Enables CAN1_RX0_IRQn (FIFO0 message pending interrupt)
 */
void HAL_CAN_MspInit(CAN_HandleTypeDef* hcan)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (hcan->Instance == CAN1)
  {
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
  }
}

/* =============================================================================
 * CAN init
 * ============================================================================= */

/**
 * @brief Initialize CAN1 (500 kbit/s "robust" timing as provided).
 *
 * Timing parameters here depend on APB clock and desired bit timing.
 * If the CAN clock changes, these values must be revalidated.
 */
void MX_CAN1_Init(void)
{
  hcan1.Instance = CAN1;

  /* Bit timing (as configured in the original code) */
  hcan1.Init.Prescaler     = 16;
  hcan1.Init.Mode          = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1      = CAN_BS1_3TQ;
  hcan1.Init.TimeSeg2      = CAN_BS2_2TQ;

  /* Robustness / behavior */
  hcan1.Init.TimeTriggeredMode      = DISABLE;
  hcan1.Init.AutoBusOff             = ENABLE;
  hcan1.Init.AutoWakeUp             = DISABLE;
  hcan1.Init.AutoRetransmission     = ENABLE;
  hcan1.Init.ReceiveFifoLocked      = DISABLE;
  hcan1.Init.TransmitFifoPriority   = DISABLE;

  if (HAL_CAN_Init(&hcan1) != HAL_OK)
    Error_Handler();
}

/* =============================================================================
 * Start + filter configuration
 * ============================================================================= */

/**
 * @brief Configure filter (accept all) and start CAN with FIFO0 RX interrupt.
 *
 * Filter configuration:
 *  - ID mask mode, 32-bit scale
 *  - All IDs accepted (ID=0, MASK=0)
 *  - Assigned to FIFO0
 */
void CAN1_Start(void)
{
  CAN_FilterTypeDef f = {0};

  f.FilterBank           = 0;
  f.FilterMode           = CAN_FILTERMODE_IDMASK;
  f.FilterScale          = CAN_FILTERSCALE_32BIT;
  f.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  f.FilterActivation     = ENABLE;

  /* For dual CAN devices; harmless on single CAN use */
  f.SlaveStartFilterBank = 14;

  /* Accept all IDs */
  f.FilterIdHigh      = 0;
  f.FilterIdLow       = 0;
  f.FilterMaskIdHigh  = 0;
  f.FilterMaskIdLow   = 0;

  if (HAL_CAN_ConfigFilter(&hcan1, &f) != HAL_OK) Error_Handler();
  if (HAL_CAN_Start(&hcan1) != HAL_OK)            Error_Handler();

  /* Enable IRQ on RX FIFO0 message pending */
  if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    Error_Handler();
}

/* =============================================================================
 * RX callback (ISR context)
 * ============================================================================= */

/**
 * @brief RX FIFO0 message pending callback.
 *
 * Decoding rules:
 *  - Only standard ID, data frames
 *  - 0x101: DLC >= 1, d[0] = heartbeat sequence
 *  - 0x120: DLC == 8, fields:
 *      * d[0..3] : lux_x100 (uint32 little-endian)
 *      * d[4..5] : full     (uint16 little-endian)
 *      * d[6..7] : ir       (uint16 little-endian)
 *
 * Important:
 *  - This runs in interrupt context. Keep it short.
 *  - It updates shared variables; getters may read them concurrently.
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  if (hcan->Instance != CAN1) return;

  CAN_RxHeaderTypeDef rh;
  uint8_t d[8];

  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rh, d) != HAL_OK) return;
  if (rh.IDE != CAN_ID_STD)  return;
  if (rh.RTR != CAN_RTR_DATA) return;

  uint32_t now = HAL_GetTick();
  s_last_tick = now;

  /* --------- 0x101: Heartbeat -------------------------------------------- */
  if (rh.StdId == 0x101u && rh.DLC >= 1u)
  {
    s_hb_seq = d[0];
    s_has101 = 1;
    s_101_tick = now;

    snprintf(s_last_txt, sizeof(s_last_txt), "HB seq=%u", (unsigned)s_hb_seq);

    strncpy(s_101_txt, s_last_txt, sizeof(s_101_txt));
    s_101_txt[sizeof(s_101_txt) - 1] = 0;
  }
  /* --------- 0x120: Light sensor ----------------------------------------- */
  else if (rh.StdId == 0x120u && rh.DLC == 8u)
  {
    s_lux_x100 = u32_le(&d[0]);
    s_full     = u16_le(&d[4]);
    s_ir       = u16_le(&d[6]);

    s_has120   = 1;
    s_120_tick = now;

    /* Present lux as integer in UI text */
    snprintf(s_last_txt, sizeof(s_last_txt),
             "LIGHT lux=%lu full=%u ir=%u",
             (unsigned long)(s_lux_x100 / 100u),
             (unsigned)s_full,
             (unsigned)s_ir);

    strncpy(s_120_txt, s_last_txt, sizeof(s_120_txt));
    s_120_txt[sizeof(s_120_txt) - 1] = 0;
  }
}

/* =============================================================================
 * Service hook (kept for app structure)
 * ============================================================================= */

/**
 * @brief Optional service hook. RX snapshots are updated by IRQ.
 *
 * Keep this function so the rest of the app can call it uniformly.
 */
void CAN1_Service(void)
{
  /* Nothing required; IRQ fills snapshots */
}

/* =============================================================================
 * UI text getters
 * ============================================================================= */

const char* CAN1_GetLastText(void)
{
  (void)s_last_tick; /* currently unused but kept for possible future timeouts */
  return s_last_txt;
}

/**
 * @brief Returns last decoded 0x101 text if fresh, otherwise "none".
 */
const char* CAN1_GetText_0x101(void)
{
  if (HAL_GetTick() - s_101_tick > 2000u) return "none";
  return s_101_txt;
}

/**
 * @brief Returns last decoded 0x120 text if fresh, otherwise "none".
 */
const char* CAN1_GetText_0x120(void)
{
  if (HAL_GetTick() - s_120_tick > 2000u) return "none";
  return s_120_txt;
}

/* =============================================================================
 * Structured getters (0x101)
 * ============================================================================= */

/**
 * @brief True if we have a recent 0x101 message.
 *
 * Note:
 *  - This function currently checks the text getter. Alternatively, you could
 *    check s_has101 + timestamp directly for slightly cleaner semantics.
 */
uint8_t CAN1_101_IsValid(void)
{
  const char *s = CAN1_GetText_0x101();
  return (s && strcmp(s, "none") != 0) ? 1u : 0u;
}

/* =============================================================================
 * Structured getters (0x120)
 * ============================================================================= */

/**
 * @brief True if the 0x120 snapshot is fresh (<= 2 seconds old).
 */
uint8_t CAN1_120_IsValid(void)
{
  return (HAL_GetTick() - s_120_tick <= 2000u) ? 1u : 0u;
}

/**
 * @brief Returns lux (integer) from the last 0x120 frame.
 *        Internally stored as lux*100, so we divide by 100 here.
 */
uint32_t CAN1_120_GetLux(void)
{
  return (uint32_t)(s_lux_x100 / 100u);
}

uint16_t CAN1_120_GetFull(void)
{
  return s_full;
}

uint16_t CAN1_120_GetIR(void)
{
  return s_ir;
}

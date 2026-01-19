/**
 * @file    i2c.c
 * @brief   I2C1 setup + periodic temperature read from ESP32 with bus recovery.
 *
 * This module provides:
 *  - I2C1 HAL initialization (PB8=SCL, PB9=SDA)
 *  - Simple error-to-string mapping for UI/debug output
 *  - I2C bus recovery routine (9 SCL pulses + STOP) for stuck SDA cases
 *  - Periodic service function that polls the ESP32 temperature (2 bytes, int16 LE)
 *  - UI-friendly getters for status/temperature/last error
 *
 * Notes:
 *  - "Recovery" is attempted once per failed read, then a retry is performed.
 *  - External pull-ups on SCL/SDA are recommended even though internal pull-ups
 *    are enabled here.
 */

#include "i2c.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * HAL handle
 * ============================================================================= */
I2C_HandleTypeDef hi2c1;

/* =============================================================================
 * Pin mapping (I2C1 on PB8/PB9)
 * ============================================================================= */
#define I2C1_SCL_PORT GPIOB
#define I2C1_SCL_PIN  GPIO_PIN_8
#define I2C1_SDA_PORT GPIOB
#define I2C1_SDA_PIN  GPIO_PIN_9

/* =============================================================================
 * Internal UI state (read by App_I2C_* getters)
 * ============================================================================= */
static uint8_t     g_i2c_ok = 0;
static float       g_i2c_temp = 0.0f;
static const char* g_i2c_last_err = "NONE";

/* =============================================================================
 * Error string helper
 * ============================================================================= */

/**
 * @brief Convert HAL I2C error flags into a compact string for logs/UI.
 * @param e HAL error flags from HAL_I2C_GetError()
 */
static const char* I2C_ErrStr(uint32_t e)
{
  if (e == HAL_I2C_ERROR_NONE)      return "NONE";
  if (e & HAL_I2C_ERROR_AF)         return "NACK";
  if (e & HAL_I2C_ERROR_TIMEOUT)    return "TIMEOUT";
  if (e & HAL_I2C_ERROR_BERR)       return "BUS";
  if (e & HAL_I2C_ERROR_ARLO)       return "ARLO";
  if (e & HAL_I2C_ERROR_OVR)        return "OVR";
  if (e & HAL_I2C_ERROR_DMA)        return "DMA";
  return "UNKNOWN";
}

/* =============================================================================
 * I2C1 initialization
 * ============================================================================= */

/**
 * @brief Initialize I2C1 with the configured timing and filters.
 *
 * Timing value:
 *  - Uses your provided timing register constant.
 *  - Must match your clock tree and desired I2C speed.
 */
void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;

  /* Timing register value (project-specific) */
  hi2c1.Init.Timing = 0x40912732;

  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;

  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;

  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Enable analog filter (helps with noise on the bus) */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief MSP init for I2C1 (GPIO + clocks). Called by HAL_I2C_Init().
 *
 * GPIO configuration:
 *  - Open-drain alternate function
 *  - Pull-up enabled (still recommended to use external pull-ups)
 */
void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (hi2c->Instance == I2C1)
  {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitStruct.Pin = I2C1_SCL_PIN | I2C1_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP; /* external pull-ups recommended */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
}

/* =============================================================================
 * I2C bus recovery
 * ============================================================================= */

/**
 * @brief Attempt to recover a stuck I2C bus (e.g., SDA held low by a slave).
 *
 * Strategy:
 *  1) Disable I2C peripheral
 *  2) Reconfigure SCL/SDA as open-drain GPIO outputs
 *  3) Generate up to 9 clock pulses on SCL while checking if SDA releases
 *  4) Generate a STOP condition (SDA low -> SCL high -> SDA high)
 *  5) Deinit and re-init I2C peripheral and GPIO AF pins
 *
 * This is a common practical recovery method for "hung bus" scenarios.
 */
static void I2C1_BusRecover(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Disable peripheral before manipulating pins manually */
  __HAL_I2C_DISABLE(&hi2c1);

  /* Configure SCL/SDA as open-drain GPIO outputs */
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  GPIO_InitStruct.Pin = I2C1_SCL_PIN;
  HAL_GPIO_Init(I2C1_SCL_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = I2C1_SDA_PIN;
  HAL_GPIO_Init(I2C1_SDA_PORT, &GPIO_InitStruct);

  /* Idle high */
  HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_SET);
  HAL_Delay(2);

  /* Generate up to 9 SCL pulses to let a stuck slave advance and release SDA */
  for (int i = 0; i < 9; i++)
  {
    if (HAL_GPIO_ReadPin(I2C1_SDA_PORT, I2C1_SDA_PIN) == GPIO_PIN_SET)
      break;

    HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
  }

  /* Force a STOP condition: SDA low -> SCL high -> SDA high */
  HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_SET);
  HAL_Delay(2);

  /* Restore peripheral state and alternate-function pins */
  (void)HAL_I2C_DeInit(&hi2c1);
  MX_I2C1_Init();
}

/* =============================================================================
 * ESP32 temperature read (protocol: 2 bytes, int16 little-endian)
 * ============================================================================= */

/**
 * @brief Read temperature from ESP32 over I2C.
 *
 * Protocol (as stated):
 *  - Master reads 2 bytes
 *  - Data is int16 little-endian
 *  - Value is interpreted as degrees Celsius (no scaling)
 *
 * @param[out] temp_celsius_out Temperature in °C as float (converted from int16)
 * @return HAL status
 */
HAL_StatusTypeDef I2C_ReadTempFromESP32(float *temp_celsius_out)
{
  if (!temp_celsius_out) return HAL_ERROR;

  uint8_t buf[2] = {0};

  /* HAL expects 8-bit address (7-bit << 1) */
  uint16_t dev = (uint16_t)(ESP32_I2C_ADDR_7BIT << 1);

  HAL_StatusTypeDef ret = HAL_I2C_Master_Receive(&hi2c1, dev, buf, 2, 200);
  if (ret != HAL_OK) return ret;

  /* int16 little-endian: buf[0] = LSB, buf[1] = MSB */
  int16_t temp_deg = (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
  *temp_celsius_out = (float)temp_deg;

  return HAL_OK;
}

/* =============================================================================
 * "Fixed" read: one recovery + retry
 * ============================================================================= */

/**
 * @brief Read temperature with one automatic recovery attempt on failure.
 *
 * Behavior:
 *  - First try: I2C_ReadTempFromESP32()
 *  - On error: store error string, print debug line, perform bus recovery
 *  - Retry once: I2C_ReadTempFromESP32()
 */
static HAL_StatusTypeDef I2C_ReadTemp_Fixed(float *out_c)
{
  HAL_StatusTypeDef st = I2C_ReadTempFromESP32(out_c);
  if (st == HAL_OK)
  {
    g_i2c_last_err = "NONE";
    return HAL_OK;
  }

  /* Capture and present error cause */
  uint32_t e = HAL_I2C_GetError(&hi2c1);
  g_i2c_last_err = I2C_ErrStr(e);

  /* Optional UART debug (requires retargeting printf) */
  printf("I2C err 0x%08lX (%s) -> recover\r\n",
         (unsigned long)e, g_i2c_last_err);

  /* Attempt bus recovery, then retry once */
  I2C1_BusRecover();

  st = I2C_ReadTempFromESP32(out_c);
  if (st == HAL_OK)
  {
    g_i2c_last_err = "NONE";
    return HAL_OK;
  }

  /* Update error string after retry */
  e = HAL_I2C_GetError(&hi2c1);
  g_i2c_last_err = I2C_ErrStr(e);

  return st;
}

/* =============================================================================
 * Periodic service function (call from main loop)
 * ============================================================================= */

/**
 * @brief Periodically poll the ESP32 temperature over I2C.
 *
 * Call pattern:
 *  - Call from main loop with a monotonic millisecond tick (e.g., HAL_GetTick()).
 *  - This function internally polls every POLL_MS.
 *
 * UI state updates:
 *  - g_i2c_ok = 1 on successful read, otherwise 0
 *  - g_i2c_temp holds the last valid temperature value
 */
void App_I2C_Service(uint32_t now_ms)
{
  enum { POLL_MS = 500 };
  static uint32_t nextPollMs = 0;

  /* Signed delta check avoids rollover issues with uint32 ticks */
  if ((int32_t)(now_ms - nextPollMs) < 0)
    return;

  nextPollMs = now_ms + POLL_MS;

  float t = 0.0f;
  if (I2C_ReadTemp_Fixed(&t) == HAL_OK)
  {
    g_i2c_temp = t;
    g_i2c_ok   = 1;
  }
  else
  {
    g_i2c_ok = 0;
  }
}

/* =============================================================================
 * UI getters (compatible with your App_UserFeedUI)
 * ============================================================================= */

uint8_t App_I2C_IsOk(void)
{
  return g_i2c_ok;
}

int App_I2C_GetTempInt(void)
{
  return (int)g_i2c_temp;
}

const char* App_I2C_GetLastErr(void)
{
  return g_i2c_last_err;
}

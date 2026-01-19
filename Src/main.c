/**
 * @file    main.c
 * @brief   Application entry point 
 *
 * This file is responsible for:
 *  - Core MCU setup (MPU, HAL, system clock)
 *  - Peripheral initialization (GPIO, UART, DMA, CAN, USB, ETH, lwIP, SPI, I2C)
 *  - Starting the application layer
 *  - Running the main loop that services all non-blocking modules
 */

#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "i2c.h"
#include "can.h"
#include "spi.h"
#include "dma.h"

#include "usb_device.h"
#include "eth.h"
#include "lwip.h"
#include "app_net.h"

#include "app_platform.h"
#include "app_helpers.h"
#include "tft.h"

#include <stdint.h>

int main(void)
{
  /* --------------------------------------------------------------------------
   * Core setup
   * -------------------------------------------------------------------------- */

  /* Configure MPU (memory attributes / cache regions) */
  MPU_Config();

  /* Initialize HAL (SysTick, NVIC priority grouping, HAL state) */
  HAL_Init();

  /* Configure system clocks (PLL, bus prescalers, flash latency) */
  SystemClock_Config();

  /* Cache disabled (can be re-enabled if needed) */
  SCB_DisableDCache();
  SCB_DisableICache();

  /* --------------------------------------------------------------------------
   * Basic peripherals
   * -------------------------------------------------------------------------- */

  MX_GPIO_Init();
  MX_USART3_UART_Init();

  /* DMA initialized early (used by SPI and potentially other peripherals) */
  MX_DMA_Init();

  /* --------------------------------------------------------------------------
   * Communication / USB
   * -------------------------------------------------------------------------- */

  MX_CAN1_Init();
  MX_USB_DEVICE_Init();

  /* --------------------------------------------------------------------------
   * Network stack
   * -------------------------------------------------------------------------- */

  MX_ETH_Init();
  MX_LWIP_Init();
  APP_NET_Init();

  /* Second GPIO init (kept exactly as in your original code) */
  MX_GPIO_Init();

  /* --------------------------------------------------------------------------
   * SPI + TFT
   * -------------------------------------------------------------------------- */

  MX_SPI1_Init();

  /* Display init + initial clear (non-blocking fill) */
  TFT_Init();
  TFT_FillColor_Async(0x0000);

  /* --------------------------------------------------------------------------
   * I2C
   * -------------------------------------------------------------------------- */

  MX_I2C1_Init();

  /* --------------------------------------------------------------------------
   * Application init
   * -------------------------------------------------------------------------- */

  App_Init();

  /* --------------------------------------------------------------------------
   * Main loop
   * -------------------------------------------------------------------------- */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* Network telemetry + lwIP processing (inside APP_NET_Service) */
    APP_NET_Service(now);

    /* USB CDC TX service */
    App_USB_Service();

    /* CAN periodic wrapper (RX is IRQ-driven) */
    App_CAN_Service(now);

    /* I2C periodic polling + recovery */
    App_I2C_Service(now);

    /* TFT driver engine + UI update */
    TFT_Task();
    App_TFT_Service(now);

    /* USB command line interface */
    App_CLI_Service(now);

    /* Periodic app tasks (e.g., logging) */
    App_Tick(now);

    /* Sleep until next interrupt if system is idle */
    if (App_USBLog_IsEmpty() && !TFT_IsBusy())
      __WFI();
  }
}

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v1.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include <string.h>
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* ---- USB CDC RX line parser (CLI) ----
 * Collect bytes until '\r' or '\n', then expose a complete line to main loop.
 * Single producer: USB RX context
 * Single consumer: main loop via CDC_ReadLine()
 */
#define CDC_RX_LINE_MAX 128

static char     g_cdc_line[CDC_RX_LINE_MAX];
static uint16_t g_cdc_line_len = 0;

static char     g_cdc_last_line[CDC_RX_LINE_MAX];
static volatile uint8_t g_cdc_line_ready = 0;

/* ---- Console TX queue (so we can echo/backspace safely) ---- */
#define CDC_CONS_TX_SZ 512
static uint8_t  g_cons_tx[CDC_CONS_TX_SZ];
static volatile uint16_t g_cons_w = 0;
static volatile uint16_t g_cons_r = 0;

static uint8_t  g_prompt_pending = 0;

/* Push bytes into console TX ring (drop on full, never block) */
static void cons_push_bytes(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
  {
    uint16_t next = (uint16_t)(g_cons_w + 1U);
    if (next >= CDC_CONS_TX_SZ) next = 0;

    if (next == g_cons_r) break; /* full -> drop */
    g_cons_tx[g_cons_w] = data[i];
    g_cons_w = next;
  }
}

static void cons_push_str(const char *s)
{
  cons_push_bytes((const uint8_t*)s, (uint16_t)strlen(s));
}

uint8_t CDC_ConsoleTxIsEmpty(void)
{
  return (g_cons_r == g_cons_w && g_prompt_pending == 0U) ? 1U : 0U;
}


/* Exposed: call from main loop to transmit queued console output */
void CDC_ConsoleTxService(void)
{
  /* send up to 64 bytes per call */
  static uint8_t chunk[64];

  /* if nothing queued, maybe send prompt */
  if (g_cons_r == g_cons_w)
  {
    if (g_prompt_pending)
    {
      g_prompt_pending = 0;
      cons_push_str("> ");
    }
    else
    {
      return;
    }
  }

  /* Build a chunk from ring (atomic-ish vs RX) */
  uint16_t n = 0;

  __disable_irq();
  while (g_cons_r != g_cons_w && n < (uint16_t)sizeof(chunk))
  {
    chunk[n++] = g_cons_tx[g_cons_r];
    uint16_t next = (uint16_t)(g_cons_r + 1U);
    if (next >= CDC_CONS_TX_SZ) next = 0;
    g_cons_r = next;
  }
  __enable_irq();

  if (n > 0U)
  {
    /* If BUSY, put bytes back by rolling back r */
    uint8_t res = CDC_Transmit_FS(chunk, n);
    if (res == USBD_BUSY)
    {
      __disable_irq();
      while (n--)
      {
        uint16_t prev = (g_cons_r == 0U) ? (CDC_CONS_TX_SZ - 1U) : (uint16_t)(g_cons_r - 1U);
        g_cons_r = prev;
      }
      __enable_irq();
    }
  }
}
/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
uint8_t CDC_ReadLine(char *out, uint16_t out_sz);
/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);

  /* greet + prompt */
  cons_push_str("Terminal ready\r\n> ");
  return (USBD_OK);
  /* USER CODE END 3 */
}

static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  return (USBD_OK);
  /* USER CODE END 4 */
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  (void)pbuf;
  (void)length;
  switch(cmd)
  {
    default:
      break;
  }
  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint.
  *         We implement a real "console feel":
  *         - echo every printable char
  *         - backspace edits buffer AND visually erases: "\b \b"
  *         - enter prints "\r\n", finalizes line, then prints prompt "> "
  *
  * IMPORTANT: We do NOT transmit directly here. We only queue output.
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  for (uint32_t i = 0; i < *Len; i++)
  {
    uint8_t b = Buf[i];

    /* Ignore ESC sequences (arrows) - drop ESC and reset current line to avoid garbage */
    if (b == 0x1B)
    {
      g_cdc_line_len = 0;
      continue;
    }

    /* Backspace: accept both BS and DEL */
    if (b == 0x08 || b == 0x7F)
    {
      if (g_cdc_line_len > 0)
      {
        g_cdc_line_len--;
        /* visually erase last char */
        cons_push_str("\b \b");
      }
      continue;
    }

    /* Enter: accept CR or LF */
    if (b == '\r' || b == '\n')
    {
      /* newline on screen */
      cons_push_str("\r\n");

      if (g_cdc_line_len > 0)
      {
        uint16_t n = g_cdc_line_len;
        if (n >= CDC_RX_LINE_MAX) n = CDC_RX_LINE_MAX - 1U;

        memcpy(g_cdc_last_line, g_cdc_line, n);
        g_cdc_last_line[n] = '\0';

        g_cdc_line_len = 0;
        g_cdc_line_ready = 1;
      }
      else
      {
        /* empty line -> just show prompt again */
      }

      /* request prompt (deferred) */
      g_prompt_pending = 1;
      continue;
    }

    /* Ignore other control chars */
    if (b < 0x20)
    {
      continue;
    }

    /* If buffer has space, append and echo */
    if (g_cdc_line_len < (CDC_RX_LINE_MAX - 1U))
    {
      g_cdc_line[g_cdc_line_len++] = (char)b;

      /* echo typed char */
      cons_push_bytes(&b, 1);
    }
    else
    {
      /* overflow -> reset line and show fresh prompt */
      g_cdc_line_len = 0;
      cons_push_str("\r\nERR: line too long\r\n");
      g_prompt_pending = 1;
    }
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return USBD_OK;
  /* USER CODE END 6 */
}

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  USBD_CDC_HandleTypeDef *hcdc =
    (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

  if (hcdc == NULL) return USBD_FAIL;
  if (hcdc->TxState != 0) return USBD_BUSY;

  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}


static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 13 */
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  /* USER CODE END 13 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

uint8_t CDC_ReadLine(char *out, uint16_t out_sz)
{
  if (!g_cdc_line_ready) return 0;
  if (out_sz == 0) return 0;

  __disable_irq();
  g_cdc_line_ready = 0;
  __enable_irq();

  strncpy(out, g_cdc_last_line, out_sz - 1U);
  out[out_sz - 1U] = '\0';
  return 1;
}

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */
void CDC_ConsolePrintSafe(const char *s)
{
  if (!s) return;

  /* Snapshot current typed line (user might be typing) */
  char snap[CDC_RX_LINE_MAX];
  uint16_t snap_len;

  __disable_irq();
  snap_len = g_cdc_line_len;
  if (snap_len >= CDC_RX_LINE_MAX) snap_len = CDC_RX_LINE_MAX - 1U;
  memcpy(snap, g_cdc_line, snap_len);
  snap[snap_len] = '\0';
  __enable_irq();

  /*
    ANSI behaviour:
    - CR + clear line:   "\r\033[2K"
    - print output on a clean line
    - redraw prompt and current buffer
  */
  cons_push_str("\r\033[2K");     /* clear current line */
  cons_push_str("\r\n");          /* go to new line for output */
  cons_push_str(s);

  /* ensure output ends with newline so prompt is on next line */
  {
    size_t L = strlen(s);
    if (L == 0 || (s[L-1] != '\n' && s[L-1] != '\r'))
      cons_push_str("\r\n");
  }

  /* redraw prompt + current buffer */
  if (snap_len > 0)
    cons_push_bytes((const uint8_t*)snap, snap_len);
}


/**
 * @file    app_helpers.c
 * @brief   Application helpers: USB CLI, debug output, CAN/TFT service glue, and UI line manager.
 *
 * This file acts as a "glue" layer between modules:
 *  - USB CDC console service + simple CLI
 *  - Periodic logging (USB)
 *  - CAN polling wrapper (even though CAN RX is IRQ-driven)
 *  - TFT UI line manager with throttled rendering (non-blocking TFT driver)
 *  - App_Init() which initializes the UI and starts peripherals/services
 *
 * Notes:
 *  - Output to USB console uses CDC_ConsolePrintSafe() to preserve user input line.
 *  - TFT rendering is chunked and non-blocking; ui_pump_once() only pushes one dirty line.
 *  - This file intentionally stays "lightweight" and does not implement heavy scheduling.
 */

#include "app_helpers.h"

/* =============================================================================
 * HAL / BSP / Middleware includes
 * ============================================================================= */
#include "usart.h"
#include "i2c.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "tft.h"
#include "can.h"
#include "app_net.h"

/* =============================================================================
 * Standard library includes
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* =============================================================================
 * Externals from other modules
 * ============================================================================= */
extern HAL_StatusTypeDef I2C_ReadTempFromESP32(float *out_temp_c);

extern void   CDC_ConsoleTxService(void);
extern uint8_t CDC_ReadLine(char *out, uint16_t out_sz);

/* =============================================================================
 * Local file-scope state (legacy / misc)
 * =============================================================================
 * NOTE: This file contains older I2C status variables, but the current UI/CLI
 * uses App_I2C_* getters from the I2C module. Consider removing these if unused.
 */
static float       g_i2c_temp = 0.0f;
static uint8_t     g_i2c_ok   = 0;
static const char* g_i2c_last_err = "none";

/* =============================================================================
 * Debug output (UART + USB console)
 * ============================================================================= */

/**
 * @brief Print debug text to UART3 and USB CDC console.
 *
 * UART is blocking with a short timeout; USB output goes through the safe console
 * print function which preserves the current user input line.
 */
static void dbg(const char *s)
{
  HAL_UART_Transmit(&huart3, (uint8_t*)s, (uint16_t)strlen(s), 200);
  CDC_ConsolePrintSafe(s);
}

/* =============================================================================
 * USB console TX service
 * ============================================================================= */

/**
 * @brief Pump the USB CDC console TX queue (non-blocking).
 *
 * The TX ring buffer lives in usbd_cdc_if.c, this simply services it.
 */
void App_USB_Service(void)
{
  CDC_ConsoleTxService();
}

/* =============================================================================
 * USB CLI: print helpers
 * ============================================================================= */

/**
 * @brief Print available commands.
 */
static void print_help(void)
{
  CDC_ConsolePrintSafe(
    "Commands:\r\n"
    "  help\r\n"
    "  status\r\n"
    "  status json\r\n"
    "  get i2c\r\n"
    "  get can\r\n"
    "  get can101\r\n"
    "  get can120\r\n"
    "  uptime\r\n"
    "  log on|off\r\n"
    "  rate <ms>\r\n"
    "  version\r\n"
  );
}

/**
 * @brief Print one compact status line (I2C + CAN).
 *
 * Note:
 *  - This uses the legacy g_i2c_* variables (as in the original code). If you
 *    want consistent output, switch to App_I2C_* getters like in App_UserFeedUI().
 */
static void print_status_line(void)
{
  char line[256];
  char i2c_txt[64];

  if (g_i2c_ok)
  {
    int temp_i = (int)g_i2c_temp;
    snprintf(i2c_txt, sizeof(i2c_txt), "Temp: %d C", temp_i);
  }
  else
  {
    snprintf(i2c_txt, sizeof(i2c_txt), "ERR: %s", g_i2c_last_err);
  }

  snprintf(line, sizeof(line),
           "[I2C]: %s | [CAN]: %s\r\n",
           i2c_txt,
           CAN1_GetLastText());

  CDC_ConsolePrintSafe(line);
}

/**
 * @brief Print I2C-only status line.
 */
static void print_i2c_line(void)
{
  char line[128];

  if (g_i2c_ok)
  {
    int temp_i = (int)g_i2c_temp;
    snprintf(line, sizeof(line), "[I2C]: Temp: %d C\r\n", temp_i);
  }
  else
  {
    snprintf(line, sizeof(line), "[I2C]: ERR: %s\r\n", g_i2c_last_err);
  }

  CDC_ConsolePrintSafe(line);
}

/**
 * @brief Print CAN-only status line.
 */
static void print_can_line(void)
{
  char line[192];
  snprintf(line, sizeof(line), "[CAN]: %s\r\n", CAN1_GetLastText());
  CDC_ConsolePrintSafe(line);
}

/**
 * @brief Print status as JSON (compact).
 */
static void print_status_json(void)
{
  char line[256];

  if (g_i2c_ok)
  {
    int temp_i = (int)g_i2c_temp;
    snprintf(line, sizeof(line),
             "{\"i2c\":{\"ok\":true,\"temp_c\":%d},\"can\":{\"text\":\"%s\"}}\r\n",
             temp_i, CAN1_GetLastText());
  }
  else
  {
    snprintf(line, sizeof(line),
             "{\"i2c\":{\"ok\":false,\"err\":\"%s\"},\"can\":{\"text\":\"%s\"}}\r\n",
             g_i2c_last_err, CAN1_GetLastText());
  }

  CDC_ConsolePrintSafe(line);
}

/* =============================================================================
 * USB CLI: state + service (command parser)
 * ============================================================================= */

/* Logging control */
static uint8_t  s_log_enabled      = 0;
static uint32_t s_last_print       = 0;
static uint32_t s_print_period_ms  = 5000U;

/**
 * @brief Process one CLI command line if available.
 *
 * Input is provided by CDC_ReadLine(), which returns complete lines only.
 * Line editing (echo/backspace/prompt) is handled in usbd_cdc_if.c.
 */
void App_CLI_Service(uint32_t now_ms)
{
  char cmd[128];

  if (!CDC_ReadLine(cmd, (uint16_t)sizeof(cmd)))
    return;

  /* Skip leading whitespace */
  char *p = cmd;
  while (*p == ' ' || *p == '\t') p++;

  if (strcmp(p, "help") == 0) {
    print_help();

  } else if (strcmp(p, "status") == 0) {
    print_status_line();

  } else if (strcmp(p, "status json") == 0) {
    print_status_json();

  } else if (strcmp(p, "get i2c") == 0) {
    print_i2c_line();

  } else if (strcmp(p, "get can") == 0) {
    print_can_line();

  } else if (strcmp(p, "uptime") == 0) {
    char line[64];
    snprintf(line, sizeof(line), "Uptime: %lu ms\r\n", (unsigned long)now_ms);
    CDC_ConsolePrintSafe(line);

  } else if (strcmp(p, "log on") == 0) {
    s_log_enabled = 1;
    s_last_print  = now_ms;
    CDC_ConsolePrintSafe("OK: log enabled\r\n");

  } else if (strcmp(p, "log off") == 0) {
    s_log_enabled = 0;
    CDC_ConsolePrintSafe("OK: log disabled\r\n");

  } else if (strcmp(p, "get can101") == 0) {
    char line[200];
    snprintf(line, sizeof(line), "[CAN101]: %s\r\n", CAN1_GetText_0x101());
    CDC_ConsolePrintSafe(line);

  } else if (strcmp(p, "get can120") == 0) {
    char line[200];
    snprintf(line, sizeof(line), "[CAN120]: %s\r\n", CAN1_GetText_0x120());
    CDC_ConsolePrintSafe(line);

  } else if (strncmp(p, "rate ", 5) == 0) {
    uint32_t ms = (uint32_t)strtoul(p + 5, NULL, 10);

    /* Clamp to sane bounds */
    if (ms < 200U)   ms = 200U;
    if (ms > 60000U) ms = 60000U;
    s_print_period_ms = ms;

    char line[64];
    snprintf(line, sizeof(line), "OK: rate=%lu ms\r\n",
             (unsigned long)s_print_period_ms);
    CDC_ConsolePrintSafe(line);

  } else if (strcmp(p, "version") == 0) {
    CDC_ConsolePrintSafe("FW: nucleo-f767-base | build: " __DATE__ " " __TIME__ "\r\n");

  } else if (*p == 0) {
    /* Empty line: ignore */

  } else {
    CDC_ConsolePrintSafe("ERR: unknown cmd. Type 'help'\r\n");
  }
}

/* =============================================================================
 * CAN: service timing wrapper
 * ============================================================================= */

static uint32_t s_last_can = 0;

/**
 * @brief Periodic CAN service wrapper.
 *
 * CAN RX is IRQ-driven, but keeping a service call is useful for consistency
 * and for future extensions (TX, timeouts, etc.).
 */
void App_CAN_Service(uint32_t now_ms)
{
  if ((uint32_t)(now_ms - s_last_can) < 50U) return;
  s_last_can = now_ms;

  CAN1_Service();
}

/* =============================================================================
 * Periodic status logging (USB)
 * ============================================================================= */

/**
 * @brief Periodically print status line if logging is enabled.
 */
static void App_Log_Service(uint32_t now_ms)
{
  if (!s_log_enabled) return;
  if ((uint32_t)(now_ms - s_last_print) < s_print_period_ms) return;

  s_last_print = now_ms;
  print_status_line();
}

/**
 * @brief Called from the main loop (or SysTick hook) with current time in ms.
 */
void App_Tick(uint32_t now_ms)
{
  App_Log_Service(now_ms);
}

/* =============================================================================
 * TFT UI helper: line manager + render pump
 * ============================================================================= */

#define UI_LINE_H    8U
#define UI_MAX_LINES 16U
#define UI_TEXT_MAX  128U

typedef struct {
  uint8_t  used;
  uint8_t  dirty;
  uint16_t fg;
  uint16_t bg;
  char     text[UI_TEXT_MAX];
  char     last[UI_TEXT_MAX];
} ui_line_t;

static ui_line_t s_ui[UI_MAX_LINES];
static uint8_t   s_ui_rr = 0;
static uint32_t  s_last_ui = 0;

/**
 * @brief Clear all UI lines.
 */
void App_UI_ClearAll(void)
{
  memset(s_ui, 0, sizeof(s_ui));
  s_ui_rr = 0;
}

/**
 * @brief Clear a single UI line.
 */
void App_UI_ClearLine(uint8_t idx)
{
  if (idx >= UI_MAX_LINES) return;
  memset(&s_ui[idx], 0, sizeof(s_ui[idx]));
}

/**
 * @brief Set a UI line text and colors; marks line dirty if content changed.
 */
void App_UI_SetLine(uint8_t idx, uint16_t fg, uint16_t bg, const char *text)
{
  if (idx >= UI_MAX_LINES) return;

  ui_line_t *L = &s_ui[idx];
  L->used = 1;
  L->fg = fg;
  L->bg = bg;

  if (!text) text = "";

  if (strncmp(L->text, text, UI_TEXT_MAX) != 0)
  {
    strncpy(L->text, text, UI_TEXT_MAX - 1);
    L->text[UI_TEXT_MAX - 1] = 0;
    L->dirty = 1;
  }
}

/**
 * @brief printf-style line setter.
 */
void App_UI_SetLineF(uint8_t idx, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
  if (idx >= UI_MAX_LINES) return;

  char buf[UI_TEXT_MAX];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  App_UI_SetLine(idx, fg, bg, buf);
}

/**
 * @brief Compute number of active lines (last "used" index + 1).
 */
static uint8_t ui_active_count(void)
{
  int last = -1;
  for (int i = 0; i < (int)UI_MAX_LINES; i++)
    if (s_ui[i].used) last = i;

  return (last < 0) ? 0U : (uint8_t)(last + 1);
}

/**
 * @brief Render one dirty/changed line (round-robin), if TFT is idle.
 *
 * This function pushes at most one line per call to keep the UI responsive
 * and avoid long SPI bursts.
 */
static void ui_pump_once(void)
{
  if (TFT_IsBusy()) return;

  uint8_t n = ui_active_count();
  if (n == 0U) return;

  for (uint8_t k = 0; k < n; k++)
  {
    uint8_t idx = (uint8_t)((s_ui_rr + k) % n);
    ui_line_t *L = &s_ui[idx];

    if (!L->used) continue;

    if (L->dirty || (strcmp(L->text, L->last) != 0))
    {
      uint16_t y = (uint16_t)(idx * UI_LINE_H);

      TFT_DrawTextLine_Async(y, L->text, L->fg, L->bg);

      strncpy(L->last, L->text, UI_TEXT_MAX - 1);
      L->last[UI_TEXT_MAX - 1] = 0;
      L->dirty = 0;

      s_ui_rr = (uint8_t)((idx + 1U) % n);
      return;
    }
  }

  /* Nothing dirty; restart round-robin */
  s_ui_rr = 0;
}

/* =============================================================================
 * TFT UI: stable layout + RGB565 helper
 * ============================================================================= */

enum {
  UI_LINE_I2C          = 0,
  UI_LINE_CAN101       = 1,
  UI_LINE_CAN120       = 2,

  UI_LINE_NET_TCP      = 6,
  UI_LINE_TCP_PAYLOAD  = 7,
  UI_LINE_NET_UDP      = 8,
  UI_LINE_NET_PAYLOAD  = 9,
};

/**
 * @brief Convert 24-bit RGB to RGB565.
 */
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(
           ((r & 0xF8) << 8) |   /* R: 5 bits */
           ((g & 0xFC) << 3) |   /* G: 6 bits */
           ((b & 0xF8) >> 3)     /* B: 5 bits */
         );
}

/* =============================================================================
 * TFT UI: user feed (overrides weak default) + default feed stub
 * ============================================================================= */

/**
 * @brief User/application UI feed.
 *
 * This function decides what to display. It sets lines in the UI manager,
 * while App_TFT_Service() handles the actual rendering throttling.
 */
void App_UserFeedUI(uint32_t now_ms)
{
  (void)now_ms;

  /* --- I2C status -------------------------------------------------------- */
  if (App_I2C_IsOk()) {
    App_UI_SetLineF(UI_LINE_I2C, rgb(255, 255, 0), 0x0000,
                    "I2C: %d C", App_I2C_GetTempInt());
  } else {
    App_UI_SetLineF(UI_LINE_I2C, rgb(255, 0, 0), 0x0000,
                    "I2C: ERR %s", App_I2C_GetLastErr());
  }

  /* --- CAN 0x101 --------------------------------------------------------- */
  if (CAN1_101_IsValid()) {
    App_UI_SetLineF(UI_LINE_CAN101, rgb(255, 0, 255), 0x0000,
                    "CAN 0x101: %s", CAN1_GetText_0x101());
  } else {
    App_UI_SetLine(UI_LINE_CAN101, rgb(255, 0, 0), 0x0000,
                   "CAN 0x101: (no data)");
  }

  /* --- CAN 0x120 (multi-line detail) ------------------------------------- */
  if (CAN1_120_IsValid())
  {
    App_UI_SetLine(UI_LINE_CAN120, rgb(255, 0, 255), 0x0000, "CAN 0x120:");
    App_UI_SetLineF(3, rgb(100, 0, 100), 0x0000, "lux : %lu", CAN1_120_GetLux());
    App_UI_SetLineF(4, rgb(100, 0, 100), 0x0000, "full: %u",  CAN1_120_GetFull());
    App_UI_SetLineF(5, rgb(100, 0, 100), 0x0000, "ir  : %u",  CAN1_120_GetIR());
  }
  else
  {
    App_UI_SetLine(UI_LINE_CAN120, rgb(255, 0, 0), 0x0000, "CAN 0x120: (no data)");
    App_UI_SetLine(3, 0x0000, 0x0000, "");
    App_UI_SetLine(4, 0x0000, 0x0000, "");
    App_UI_SetLine(5, 0x0000, 0x0000, "");
  }

  /* --- Network ----------------------------------------------------------- */
  if (APP_NET_TcpIsConnected())
    App_UI_SetLineF(UI_LINE_NET_TCP, rgb(0, 255, 255), 0x0000, "NET TCP: UP");
  else
    App_UI_SetLineF(UI_LINE_NET_TCP, rgb(0, 255, 255), 0x0000, "NET TCP: DOWN");

  App_UI_SetLineF(UI_LINE_TCP_PAYLOAD, rgb(0, 100, 100), 0x0000,
                  "TCP: %s", APP_NET_GetLastTCP());

  App_UI_SetLineF(UI_LINE_NET_UDP, rgb(0, 255, 255), 0x0000,
                  "NET UDP: TX 1Hz");

  App_UI_SetLineF(UI_LINE_NET_PAYLOAD, rgb(0, 100, 100), 0x0000,
                  "UDP: %s", APP_NET_GetLastUDP());
}

/**
 * @brief Default feed stub.
 *
 * Leave empty or use it for lines that are not covered by App_UserFeedUI().
 * (Your comment: do not render CAN 0x120 here again.)
 */
static void App_DefaultFeedUI(void)
{
  /* Intentionally left blank */
}

/* =============================================================================
 * TFT service: feed + throttled render pump
 * ============================================================================= */

/**
 * @brief Drive the TFT UI.
 *
 *  - Updates the desired text lines (feeds)
 *  - Pushes at most one changed line every 50ms
 */
void App_TFT_Service(uint32_t now_ms)
{
  App_DefaultFeedUI();
  App_UserFeedUI(now_ms);

  if ((uint32_t)(now_ms - s_last_ui) < 50U) return;
  s_last_ui = now_ms;

  ui_pump_once();
}

/* =============================================================================
 * App init
 * ============================================================================= */

/**
 * @brief Initialize application-level services and UI.
 *
 * Expected call order:
 *  - Peripherals (HAL init, clocks, GPIO, etc.) are already configured
 *  - Then App_Init() is called to start higher-level services
 */
void App_Init(void)
{
  dbg("Boot OK\r\n");

  CAN1_Start();

  TFT_Init();
  TFT_FillColor_Async(0x0000);
  dbg("TFT init OK\r\n");

  /* Reset UI line manager after display init */
  App_UI_ClearAll();

  dbg("USB CDC init OK\r\n");
  dbg("Type 'help' over USB CDC\r\n");

  s_last_can   = HAL_GetTick();
  s_last_ui    = HAL_GetTick();
  s_last_print = HAL_GetTick();
}

/* =============================================================================
 * Misc
 * ============================================================================= */

/**
 * @brief Helper for checking if USB log queue is empty.
 */
uint8_t App_USBLog_IsEmpty(void)
{
  return CDC_ConsoleTxIsEmpty();
}

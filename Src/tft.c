/**
 * @file    tft.c
 * @brief   Minimal TFT driver (SPI) for STM32F7 with a small non-blocking render engine.
 *
 * Features:
 *  - Low-level command/data write helpers (SPI, CS/DC/RST)
 *  - Address window setup (COL/ROW/RAMWR)
 *  - Non-blocking operations:
 *      * Full-screen fill (RGB565)
 *      * Single text line blit using a 5x7 font (buffer: 160x8)
 *  - Legacy blocking wrappers kept for backward compatibility
 *
 * Notes:
 *  - This implementation uses HAL_SPI_Transmit (polling).
 *  - "Async" here means chunked/polled via TFT_Task(), not DMA/interrupt driven.
 */

#include "tft.h"
#include "spi.h"
#include "gpio.h"
#include "stm32f7xx_hal.h"
#include <string.h>
#include <stdint.h>

/* =============================================================================
 * Pin mapping (confirmed)
 * ============================================================================= */
#define TFT_RST_PORT GPIOF
#define TFT_RST_PIN  GPIO_PIN_13   /* D7 */

#define TFT_CS_PORT  GPIOE
#define TFT_CS_PIN   GPIO_PIN_11   /* D6 */

#define TFT_DC_PORT  GPIOE
#define TFT_DC_PIN   GPIO_PIN_9    /* D5 */

/* =============================================================================
 * Display geometry / orientation
 * Landscape logical size: 160 x 128
 * ============================================================================= */
#define TFT_W       160U
#define TFT_H       128U
#define TFT_PIXELS  (TFT_W * TFT_H)

/* =============================================================================
 * Font metrics: 5x7 + 1px spacing => 6px per character cell
 * Line height is 8px (7px glyph + 1px padding)
 * ============================================================================= */
#define FONT_W     5U
#define FONT_H     7U
#define CHAR_SP    1U
#define CELL_W     (FONT_W + CHAR_SP)
#define LINE_H     8U
#define MAX_CHARS  (TFT_W / CELL_W)  /* 160/6 = 26 */

/* =============================================================================
 * Low-level GPIO helpers
 * ============================================================================= */
static inline void CS_L(void){ HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET); }
static inline void CS_H(void){ HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);   }
static inline void DC_L(void){ HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_RESET); }
static inline void DC_H(void){ HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);   }
static inline void RST_L(void){ HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_RESET); }
static inline void RST_H(void){ HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);   }

/* =============================================================================
 * SPI write primitives
 * ============================================================================= */

/**
 * @brief Send a single-byte command.
 */
static void tft_cmd(uint8_t c)
{
  CS_L(); DC_L();
  (void)HAL_SPI_Transmit(&hspi1, &c, 1, 10);
  CS_H();
}

/**
 * @brief Send a single byte of data.
 */
static void tft_data8(uint8_t d)
{
  CS_L(); DC_H();
  (void)HAL_SPI_Transmit(&hspi1, &d, 1, 10);
  CS_H();
}

/**
 * @brief Set the active drawing window and start RAM write (RAMWR).
 *        Subsequent data bytes will fill the window in display order.
 */
static void TFT_SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  tft_cmd(0x2A); /* COL (column address set) */
  tft_data8((uint8_t)(x0 >> 8)); tft_data8((uint8_t)(x0 & 0xFF));
  tft_data8((uint8_t)(x1 >> 8)); tft_data8((uint8_t)(x1 & 0xFF));

  tft_cmd(0x2B); /* ROW (row address set) */
  tft_data8((uint8_t)(y0 >> 8)); tft_data8((uint8_t)(y0 & 0xFF));
  tft_data8((uint8_t)(y1 >> 8)); tft_data8((uint8_t)(y1 & 0xFF));

  tft_cmd(0x2C); /* RAMWR (memory write) */
}

/* =============================================================================
 * Non-blocking engine: chunked fill + chunked blit
 * ============================================================================= */

/* Chunk size per TFT_Task() call (bytes). Must fit into g_txbuf. */
#ifndef TFT_CHUNK_BYTES
#define TFT_CHUNK_BYTES 512U
#endif

typedef enum {
  TFT_OP_NONE = 0,
  TFT_OP_FILL,
  TFT_OP_BLIT
} tft_op_t;

/* Current operation state */
static volatile tft_op_t g_op = TFT_OP_NONE;

/* ---- Fill state ---- */
static uint16_t g_fill_color = 0;
static uint32_t g_fill_sent_pixels = 0;

/* ---- Blit state ---- */
static const uint8_t *g_blit_ptr = NULL;  /* Points to source pixel stream (RGB565) */
static uint32_t g_blit_len = 0;           /* Total bytes to send */
static uint32_t g_blit_sent = 0;          /* Bytes already sent */

/* Temporary TX buffer for chunked sending */
static uint8_t g_txbuf[TFT_CHUNK_BYTES];

/* =============================================================================
 * Tiny 5x7 font (ASCII 32..126)
 * Stored as 5 columns per glyph, LSB at top row.
 * ============================================================================= */
static const uint8_t font5x7[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

/**
 * @brief Returns the 5-column glyph bitmap for ASCII c (32..126).
 *        Out-of-range characters map to space.
 */
static inline const uint8_t* glyph_for(char c)
{
  if (c < 32 || c > 126) c = ' ';
  return font5x7[(uint8_t)c - 32U];
}

/* =============================================================================
 * Line buffer for text rendering: 160 x 8 pixels, RGB565 => 160*8*2 = 2560 bytes
 * ============================================================================= */
static uint16_t g_linebuf[TFT_W * LINE_H];

static inline void linebuf_set(uint16_t x, uint16_t y, uint16_t c)
{
  if (x >= TFT_W || y >= LINE_H) return;
  g_linebuf[y * TFT_W + x] = c;
}

/**
 * @brief Fill the entire line buffer with a background color.
 */
static void linebuf_clear(uint16_t bg)
{
  for (uint32_t i = 0; i < (TFT_W * LINE_H); i++)
    g_linebuf[i] = bg;
}

/**
 * @brief Draw a single 5x7 character into the line buffer.
 * @param x   Left coordinate within the line buffer
 * @param y   Top coordinate within the line buffer (normally 0)
 * @param ch  ASCII character
 * @param fg  Foreground RGB565
 * @param bg  Background RGB565 (also used for spacing column)
 */
static void linebuf_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
  const uint8_t *g = glyph_for(ch);

  /* 5 columns of bitmap data */
  for (uint16_t col = 0; col < FONT_W; col++)
  {
    uint8_t bits = g[col];
    for (uint16_t row = 0; row < FONT_H; row++)
    {
      uint16_t color = (bits & (1U << row)) ? fg : bg;
      linebuf_set((uint16_t)(x + col), (uint16_t)(y + row), color);
    }
  }

  /* 1-column spacing to the right of the glyph */
  for (uint16_t row = 0; row < FONT_H; row++)
    linebuf_set((uint16_t)(x + FONT_W), (uint16_t)(y + row), bg);
}

/* =============================================================================
 * Public API
 * ============================================================================= */

uint16_t TFT_Width(void)  { return (uint16_t)TFT_W; }
uint16_t TFT_Height(void) { return (uint16_t)TFT_H; }

/**
 * @brief Start a non-blocking full-screen fill.
 *        Progress is done in TFT_Task() chunks.
 * @note  If you call this while another operation is running, it will overwrite the state.
 */
void TFT_FillColor_Async(uint16_t color565)
{
  g_fill_color = color565;
  g_fill_sent_pixels = 0;

  /* Prepare full-screen window and RAM write */
  TFT_SetAddrWindow(0, 0, (uint16_t)(TFT_W - 1U), (uint16_t)(TFT_H - 1U));

  /* Keep CS low and DC high while streaming pixel data */
  CS_L();
  DC_H();
  g_op = TFT_OP_FILL;
}

/**
 * @brief Draw one text line (8px tall) at y, non-blocking.
 *        Uses a local line buffer, then blits it in chunks via TFT_Task().
 *
 * Constraints:
 *  - Only one operation at a time (returns immediately if busy).
 *  - Text is clipped to MAX_CHARS and optionally replaced with "..." if longer.
 */
void TFT_DrawTextLine_Async(uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
  if (y >= TFT_H) return;
  if (g_op != TFT_OP_NONE) return;

  /* Ensure the 8px line fits entirely on screen */
  if (y > (TFT_H - LINE_H)) y = (uint16_t)(TFT_H - LINE_H);

  /* Render line into RAM buffer first */
  linebuf_clear(bg);

  char tmp[MAX_CHARS + 1];
  if (!text) text = "";
  strncpy(tmp, text, MAX_CHARS);
  tmp[MAX_CHARS] = 0;

  /* Add ellipsis if the original string was longer */
  if (strlen(text) > MAX_CHARS && MAX_CHARS >= 3)
  {
    tmp[MAX_CHARS - 3] = '.';
    tmp[MAX_CHARS - 2] = '.';
    tmp[MAX_CHARS - 1] = '.';
    tmp[MAX_CHARS]     = 0;
  }

  /* Draw characters left-to-right */
  uint16_t x = 0;
  for (uint16_t i = 0; i < MAX_CHARS && tmp[i] != 0; i++)
  {
    linebuf_draw_char(x, 0, tmp[i], fg, bg);
    x = (uint16_t)(x + CELL_W);
    if (x >= TFT_W) break;
  }

  /* Prepare TFT window for this line and start streaming */
  TFT_SetAddrWindow(0, y, (uint16_t)(TFT_W - 1U), (uint16_t)(y + LINE_H - 1U));

  g_blit_ptr  = (const uint8_t*)g_linebuf;
  g_blit_len  = (uint32_t)(TFT_W * LINE_H * 2U);
  g_blit_sent = 0;

  CS_L();
  DC_H();
  g_op = TFT_OP_BLIT;
}

/**
 * @brief Progress the current async operation (fill or blit).
 *        Call regularly from the main loop.
 *
 * Implementation detail:
 *  - Each call sends up to TFT_CHUNK_BYTES (rounded to even) over SPI.
 *  - When finished, CS is released and g_op returns to TFT_OP_NONE.
 */
void TFT_Task(void)
{
  if (g_op == TFT_OP_NONE) return;

  /* ------------------------- FILL ------------------------- */
  if (g_op == TFT_OP_FILL)
  {
    uint32_t remaining = TFT_PIXELS - g_fill_sent_pixels;
    if (remaining == 0U)
    {
      CS_H();
      g_op = TFT_OP_NONE;
      return;
    }

    /* Ensure even number of bytes (RGB565 = 2 bytes per pixel) */
    uint32_t bytes = TFT_CHUNK_BYTES;
    if (bytes & 1U) bytes--;

    uint32_t max_pixels  = bytes / 2U;
    uint32_t this_pixels = remaining;
    if (this_pixels > max_pixels) this_pixels = max_pixels;

    uint8_t hi = (uint8_t)(g_fill_color >> 8);
    uint8_t lo = (uint8_t)(g_fill_color & 0xFF);
    uint32_t this_bytes = this_pixels * 2U;

    /* Expand one RGB565 value into the TX buffer */
    for (uint32_t i = 0; i < this_bytes; i += 2U)
    {
      g_txbuf[i + 0U] = hi;
      g_txbuf[i + 1U] = lo;
    }

    /* Polling transmit; if busy/error, just try again next call */
    if (HAL_SPI_Transmit(&hspi1, g_txbuf, (uint16_t)this_bytes, 2) != HAL_OK)
      return;

    g_fill_sent_pixels += this_pixels;

    if (g_fill_sent_pixels >= TFT_PIXELS)
    {
      CS_H();
      g_op = TFT_OP_NONE;
    }
    return;
  }

  /* ------------------------- BLIT ------------------------- */
  if (g_op == TFT_OP_BLIT)
  {
    uint32_t remaining = g_blit_len - g_blit_sent;
    if (remaining == 0U)
    {
      CS_H();
      g_op = TFT_OP_NONE;
      return;
    }

    uint32_t max_pixels  = TFT_CHUNK_BYTES / 2U;
    uint32_t this_pixels = remaining / 2U;
    if (this_pixels > max_pixels) this_pixels = max_pixels;

    /* Source points into the line buffer (RGB565, CPU-endian) */
    const uint16_t *src =
        (const uint16_t *)g_blit_ptr + (g_blit_sent / 2U);

    /* Convert to big-endian byte stream for the TFT */
    for (uint32_t i = 0; i < this_pixels; i++)
    {
      uint16_t c = src[i];
      g_txbuf[2U*i + 0U] = (uint8_t)(c >> 8);
      g_txbuf[2U*i + 1U] = (uint8_t)(c & 0xFF);
    }

    uint32_t this_bytes = this_pixels * 2U;

    if (HAL_SPI_Transmit(&hspi1, g_txbuf, (uint16_t)this_bytes, 2) != HAL_OK)
      return;

    g_blit_sent += this_bytes;

    if (g_blit_sent >= g_blit_len)
    {
      CS_H();
      g_op = TFT_OP_NONE;
    }
    return;
  }
}

/**
 * @brief Returns 1 while an operation is active.
 */
uint8_t TFT_IsBusy(void)
{
  return (g_op != TFT_OP_NONE);
}

/* =============================================================================
 * Legacy blocking wrappers (keep older code working)
 * ============================================================================= */

/**
 * @brief Blocking fill using the async engine under the hood.
 */
void TFT_FillColor(uint16_t color565)
{
  TFT_FillColor_Async(color565);
  while (TFT_IsBusy())
    TFT_Task();
}

/**
 * @brief Simple blocking RGB demo.
 */
void TFT_RGB_Cycle(void)
{
  TFT_FillColor(0xF800);  /* Red */
  HAL_Delay(200);
  TFT_FillColor(0x07E0);  /* Green */
  HAL_Delay(200);
  TFT_FillColor(0x001F);  /* Blue */
  HAL_Delay(200);
}

/* =============================================================================
 * Optional: non-blocking RGB cycle state machine
 * ============================================================================= */
typedef struct {
  uint8_t  phase;        /* 0..2 */
  uint32_t phase_start;  /* tick timestamp at phase start */
  uint8_t  running;      /* 1 = active */
  uint32_t hold_ms;      /* how long to hold each color */
} tft_cycle_t;

static tft_cycle_t g_cycle = {0};

/**
 * @brief Start non-blocking RGB cycling.
 * @param hold_ms Hold time per color; if 0, defaults to 700ms.
 */
void TFT_RGB_Cycle_Start(uint32_t hold_ms)
{
  g_cycle.phase = 0;
  g_cycle.phase_start = HAL_GetTick();
  g_cycle.running = 1;
  g_cycle.hold_ms = (hold_ms == 0U) ? 700U : hold_ms;

  TFT_FillColor_Async(0xF800);
}

/**
 * @brief Stop non-blocking RGB cycling.
 */
void TFT_RGB_Cycle_Stop(void)
{
  g_cycle.running = 0;
}

/**
 * @brief Call regularly; advances TFT transfer and updates cycle phases.
 */
void TFT_RGB_Cycle_Task(void)
{
  TFT_Task();

  if (!g_cycle.running) return;
  if (TFT_IsBusy()) return;

  uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - g_cycle.phase_start) < g_cycle.hold_ms) return;

  g_cycle.phase_start = now;
  g_cycle.phase = (uint8_t)((g_cycle.phase + 1U) % 3U);

  if (g_cycle.phase == 0U)      TFT_FillColor_Async(0xF800);
  else if (g_cycle.phase == 1U) TFT_FillColor_Async(0x07E0);
  else                          TFT_FillColor_Async(0x001F);
}

/* =============================================================================
 * Initialization (Landscape via MADCTL)
 * ============================================================================= */

/**
 * @brief Initialize TFT controller.
 *
 * Sequence:
 *  - Hardware reset
 *  - Sleep out
 *  - Pixel format: RGB565
 *  - Memory access control (MADCTL) for landscape
 *  - Display on
 *
 * Note:
 *  MADCTL value can differ between controllers/modules (ST7735/ST7789 variants).
 *  0x60 is commonly used for landscape on many boards.
 */
void TFT_Init(void)
{
  RST_L();
  HAL_Delay(50);
  RST_H();
  HAL_Delay(120);

  tft_cmd(0x11); HAL_Delay(120);    /* SLPOUT */
  tft_cmd(0x3A); tft_data8(0x05);   /* COLMOD: 16-bit (RGB565) */

  tft_cmd(0x36); tft_data8(0x60);   /* MADCTL: landscape mode (module dependent) */

  tft_cmd(0x29); HAL_Delay(20);     /* DISPON */

  g_op = TFT_OP_NONE;
  g_cycle.running = 0;
}

/**
 * @file    app_net.c
 * @brief   Lightweight UDP/TCP telemetry transport using lwIP (NO_SYS mode).
 *
 * This module provides:
 *  - UDP fire-and-forget telemetry sender
 *  - TCP client with automatic reconnect and single-message TX buffering
 *  - Periodic lwIP polling (CubeMX NO_SYS integration)
 *  - Small UI/debug helpers exposing last sent payload snippets
 *
 * Design goals:
 *  - Simple, robust networking without an RTOS
 *  - Non-blocking main-loop driven operation
 *  - Safe interaction with lwIP callbacks (minimal state, no dynamic queues)
 *
 * Assumptions:
 *  - lwIP is configured in NO_SYS mode (CubeMX default)
 *  - ethernetif_input() is used (polling, not IRQ-driven)
 *  - gnetif is provided by CubeMX lwIP glue code
 */

#include "app_net.h"

#include <string.h>
#include <stdio.h>

#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "ethernetif.h"
#include "lwip.h"          /* MX_LWIP_Process() */

#include "app_helpers.h"   /* App_I2C_GetTempInt(), etc. */
#include "can.h"           /* CAN1_GetText_0x101(), CAN1_GetText_0x120() */

/* gnetif is created by CubeMX lwIP glue code */
extern struct netif gnetif;

/* =============================================================================
 * Last payload snippets (for UI / debug)
 * ============================================================================= */
static char g_udp_last[64] = "-";
static char g_tcp_last[64] = "-";

const char* APP_NET_GetLastUDP(void) { return g_udp_last; }
const char* APP_NET_GetLastTCP(void) { return g_tcp_last; }

/* =============================================================================
 * Internal network state
 * ============================================================================= */
static ip_addr_t g_remote_ip;
static uint16_t  g_udp_port = APP_UDP_PORT;
static uint16_t  g_tcp_port = APP_TCP_PORT;

/* UDP control block */
static struct udp_pcb *g_udp = NULL;

/* =============================================================================
 * TCP client state
 * ============================================================================= */
typedef enum {
  TCP_DOWN = 0,
  TCP_CONNECTING,
  TCP_UP
} tcp_state_t;

static struct tcp_pcb *g_tcp = NULL;
static tcp_state_t     g_tcp_state = TCP_DOWN;

/* next allowed reconnect attempt (ms) */
static uint32_t g_next_tcp_reconnect_ms = 0;

/* Single-message TX buffer (one in-flight packet at a time) */
static char     g_tcp_txbuf[256];
static uint16_t g_tcp_txlen = 0;

/* =============================================================================
 * Helpers
 * ============================================================================= */

/**
 * @brief Parse dotted IPv4 string into lwIP ip_addr_t.
 */
static bool parse_ip(const char *ip_str, ip_addr_t *out)
{
  if (!ip_str || !out) return false;
  return (ipaddr_aton(ip_str, out) != 0);
}

/* =============================================================================
 * UDP
 * ============================================================================= */

/**
 * @brief Create UDP PCB once (lazy init).
 */
static void udp_init_once(void)
{
  if (g_udp) return;
  g_udp = udp_new_ip_type(IPADDR_TYPE_V4);
}

/**
 * @brief Send telemetry via UDP (fire-and-forget).
 *
 * Payload format (JSON-like):
 *  {
 *    "ts": <ms>,
 *    "i2c": <temp>,
 *    "can101": "<text>",
 *    "can120": "<text>"
 *  }
 */
bool APP_NET_SendUDP(const AppTelemetry *t)
{
  if (!t) return false;

  if (!g_udp)
    udp_init_once();
  if (!g_udp)
    return false;

  char msg[256];
  int n = snprintf(msg, sizeof(msg),
                   "{\"ts\":%lu,\"i2c\":%ld,\"can101\":\"%s\",\"can120\":\"%s\"}\n",
                   (unsigned long)t->now_ms,
                   (long)t->i2c_temp_c,
                   t->can_0x101,
                   t->can_0x120);
  if (n <= 0) return false;
  if (n >= (int)sizeof(msg)) n = (int)sizeof(msg) - 1;

  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
  if (!p) return false;

  memcpy(p->payload, msg, (size_t)n);
  err_t err = udp_sendto(g_udp, p, &g_remote_ip, g_udp_port);
  pbuf_free(p);

  return (err == ERR_OK);
}

/* =============================================================================
 * TCP helpers
 * ============================================================================= */

/**
 * @brief Gracefully close TCP connection and reset state.
 */
static void app_tcp_close(void)
{
  if (g_tcp) {
    tcp_arg(g_tcp, NULL);
    tcp_err(g_tcp, NULL);
    tcp_recv(g_tcp, NULL);
    tcp_sent(g_tcp, NULL);
    tcp_poll(g_tcp, NULL, 0);
    tcp_close(g_tcp);
    g_tcp = NULL;
  }

  g_tcp_state = TCP_DOWN;
  g_tcp_txlen = 0;
}

/**
 * @brief Force-abort TCP connection (used on hard errors).
 */
static void tcp_force_abort(void)
{
  if (g_tcp) {
    tcp_abort(g_tcp);
    g_tcp = NULL;
  }

  g_tcp_state = TCP_DOWN;
  g_tcp_txlen = 0;
}

/**
 * @brief Called by lwIP when TX buffer was acknowledged.
 */
static err_t on_tcp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  (void)arg; (void)tpcb; (void)len;
  g_tcp_txlen = 0;
  return ERR_OK;
}

/**
 * @brief Called by lwIP on TCP error/reset.
 */
static void on_tcp_err(void *arg, err_t err)
{
  (void)arg; (void)err;
  g_tcp = NULL;
  g_tcp_state = TCP_DOWN;
  g_tcp_txlen = 0;
}

/**
 * @brief TCP receive callback.
 *
 * Incoming data is ignored; connection is kept alive.
 */
static err_t on_tcp_recv(void *arg, struct tcp_pcb *tpcb,
                         struct pbuf *p, err_t err)
{
  (void)arg; (void)err;

  if (!p) {
    /* Remote closed connection */
    app_tcp_close();
    return ERR_OK;
  }

  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

/**
 * @brief TCP connected callback.
 */
static err_t on_tcp_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
  (void)arg;

  if (err != ERR_OK) {
    tcp_force_abort();
    return err;
  }

  g_tcp_state = TCP_UP;

  tcp_recv(tpcb, on_tcp_recv);
  tcp_sent(tpcb, on_tcp_sent);
  tcp_err(tpcb, on_tcp_err);

  return ERR_OK;
}

/**
 * @brief Start TCP connection attempt (client).
 */
static void tcp_start_connect(uint32_t now_ms)
{
  if (g_tcp_state == TCP_UP || g_tcp_state == TCP_CONNECTING)
    return;

  g_tcp = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!g_tcp) {
    g_tcp_state = TCP_DOWN;
    g_next_tcp_reconnect_ms = now_ms + 2000;
    return;
  }

  g_tcp_state = TCP_CONNECTING;
  tcp_err(g_tcp, on_tcp_err);

  err_t e = tcp_connect(g_tcp, &g_remote_ip, g_tcp_port, on_tcp_connected);
  if (e != ERR_OK) {
    tcp_force_abort();
    g_next_tcp_reconnect_ms = now_ms + 2000;
  }
}

/**
 * @brief Check if TCP connection is currently established.
 */
bool APP_NET_TcpIsConnected(void)
{
  return (g_tcp_state == TCP_UP) && (g_tcp != NULL);
}

/**
 * @brief Send telemetry over TCP (one message at a time).
 *
 * Behavior:
 *  - Returns false if not connected or if a TX is already in flight.
 *  - TX buffer is released in on_tcp_sent().
 */
bool APP_NET_SendTCP(const AppTelemetry *t)
{
  if (!t) return false;
  if (!APP_NET_TcpIsConnected()) return false;
  if (g_tcp_txlen != 0) return false;

  int n = snprintf(g_tcp_txbuf, sizeof(g_tcp_txbuf),
                   "{\"ts\":%lu,\"i2c\":%ld,\"can101\":\"%s\",\"can120\":\"%s\"}\n",
                   (unsigned long)t->now_ms,
                   (long)t->i2c_temp_c,
                   t->can_0x101,
                   t->can_0x120);
  if (n <= 0) return false;
  if (n >= (int)sizeof(g_tcp_txbuf)) n = (int)sizeof(g_tcp_txbuf) - 1;

  g_tcp_txlen = (uint16_t)n;

  if (tcp_write(g_tcp, g_tcp_txbuf, g_tcp_txlen,
                TCP_WRITE_FLAG_COPY) != ERR_OK) {
    g_tcp_txlen = 0;
    return false;
  }

  if (tcp_output(g_tcp) != ERR_OK) {
    g_tcp_txlen = 0;
    return false;
  }

  return true;
}

/* =============================================================================
 * Public API
 * ============================================================================= */

/**
 * @brief Initialize networking module.
 */
void APP_NET_Init(void)
{
  parse_ip(APP_RASPI_IP, &g_remote_ip);
  udp_init_once();

  g_tcp_state = TCP_DOWN;
  g_next_tcp_reconnect_ms = 0;
}

/**
 * @brief Update remote IP and ports.
 *
 * Resets TCP connection when changed.
 */
bool APP_NET_SetRemote(const char *ip_str,
                       uint16_t udp_port,
                       uint16_t tcp_port)
{
  ip_addr_t ip;
  if (!parse_ip(ip_str, &ip)) return false;

  g_remote_ip = ip;
  g_udp_port  = udp_port;
  g_tcp_port  = tcp_port;

  app_tcp_close();
  return true;
}

/**
 * @brief Low-level lwIP pump (NO_SYS mode).
 *
 * Must be called regularly from main loop.
 */
void APP_NET_Poll(uint32_t now_ms)
{
  (void)now_ms;

  MX_LWIP_Process();
  ethernetif_input(&gnetif);
  sys_check_timeouts();

  /* Handle TCP reconnect attempts */
  if (!APP_NET_TcpIsConnected()) {
    if (g_next_tcp_reconnect_ms == 0 ||
        (int32_t)(now_ms - g_next_tcp_reconnect_ms) >= 0) {
      tcp_start_connect(now_ms);
      g_next_tcp_reconnect_ms = now_ms + 2000;
    }
  }
}

/* =============================================================================
 * Main-loop service (called from main.c)
 * ============================================================================= */

/**
 * @brief Periodic network service.
 *
 * Timing:
 *  - lwIP pump: every 10 ms
 *  - Telemetry send: every 1000 ms
 */
void APP_NET_Service(uint32_t now_ms)
{
  static uint32_t lwip_tick = 0;
  static uint32_t send_tick = 0;

  /* 10 ms lwIP processing */
  if ((int32_t)(now_ms - lwip_tick) >= 0) {
    APP_NET_Poll(now_ms);
    lwip_tick = now_ms + 10;
  }

  /* 1 Hz telemetry */
  if ((int32_t)(now_ms - send_tick) >= 0) {
    AppTelemetry t = {0};

    t.now_ms     = now_ms;
    t.i2c_temp_c = (int32_t)App_I2C_GetTempInt();

    snprintf(t.can_0x101, sizeof(t.can_0x101), "%s",
             CAN1_GetText_0x101());
    snprintf(t.can_0x120, sizeof(t.can_0x120), "%s",
             CAN1_GetText_0x120());

    snprintf(g_udp_last, sizeof(g_udp_last),
             "ts=%lu i2c=%ld",
             (unsigned long)t.now_ms,
             (long)t.i2c_temp_c);

    snprintf(g_tcp_last, sizeof(g_tcp_last),
             "C101=%.58s", t.can_0x101);

    (void)APP_NET_SendUDP(&t);
    (void)APP_NET_SendTCP(&t);

    send_tick = now_ms + 1000;
  }
}

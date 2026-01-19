// =======================================================
// ESP32 (Arduino) + TWAI (CAN) + TSL2591 Light Sensor
//
// Purpose:
// - Read ambient light from TSL2591 over I2C
// - Transmit data via CAN (TWAI driver) with robust auto-recovery
//
// Wiring:
// CAN (TWAI):
//   TX = GPIO16
//   RX = GPIO17
// I2C (TSL2591):
//   SDA = GPIO21
//   SCL = GPIO22
//
// CAN Frames (11-bit standard IDs):
//   0x101 Heartbeat: [seq u8]
//   0x120 Light:     [lux_x100 u32][full u16][ir u16]  (Little Endian)
// =======================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>

extern "C" {
  #include "driver/twai.h"
}

// ---------------- Hardware Pins ----------------
static constexpr gpio_num_t CAN_TX_GPIO = GPIO_NUM_16;
static constexpr gpio_num_t CAN_RX_GPIO = GPIO_NUM_17;
static constexpr uint8_t    I2C_SDA_PIN = 21;
static constexpr uint8_t    I2C_SCL_PIN = 22;

// ---------------- CAN Bitrate ----------------
#define USE_500K 1
#if USE_500K
  #define TWAI_TIMING_CFG TWAI_TIMING_CONFIG_500KBITS()
#else
  #define TWAI_TIMING_CFG TWAI_TIMING_CONFIG_250KBITS()
#endif

// ---------------- Timing ----------------
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;
static constexpr uint32_t LIGHT_INTERVAL_MS     = 500;
static constexpr uint32_t MONITOR_INTERVAL_MS   = 2000;

// ---------------- CAN IDs ----------------
static constexpr uint32_t CAN_ID_HEARTBEAT = 0x101;
static constexpr uint32_t CAN_ID_LIGHT     = 0x120;

// ---------------- Globals ----------------
static bool     g_twAIStarted = false;
static uint32_t g_lastHbMs    = 0;
static uint32_t g_lastLightMs = 0;
static uint32_t g_lastMonMs   = 0;
static uint8_t  g_hbSeq       = 0;

// ---------------- Sensor ----------------
static Adafruit_TSL2591 g_tsl = Adafruit_TSL2591(2591);

// -----------------------------------------------------
// Helpers
// -----------------------------------------------------
static const char* twaiStateToStr(twai_state_t s)
{
  switch (s) {
    case TWAI_STATE_STOPPED:     return "STOPPED";
    case TWAI_STATE_RUNNING:     return "RUNNING";
    case TWAI_STATE_BUS_OFF:     return "BUS_OFF";
    case TWAI_STATE_RECOVERING:  return "RECOVERING";
    default:                     return "UNKNOWN";
  }
}

static void printTwaiStatus(const char* tag)
{
  twai_status_info_t st;
  if (twai_get_status_info(&st) == ESP_OK) {
    Serial.printf("[%s] state=%s txq=%d rxq=%d txerr=%d rxerr=%d buserr=%lu arb_lost=%lu\r\n",
                  tag,
                  twaiStateToStr(st.state),
                  st.msgs_to_tx, st.msgs_to_rx,
                  st.tx_error_counter, st.rx_error_counter,
                  (unsigned long)st.bus_error_count,
                  (unsigned long)st.arb_lost_count);
  } else {
    Serial.printf("[%s] twai_get_status_info failed\r\n", tag);
  }
}

static void packU32LE(uint8_t* dst, uint32_t v)
{
  dst[0] = (uint8_t)(v >> 0);
  dst[1] = (uint8_t)(v >> 8);
  dst[2] = (uint8_t)(v >> 16);
  dst[3] = (uint8_t)(v >> 24);
}

static void packU16LE(uint8_t* dst, uint16_t v)
{
  dst[0] = (uint8_t)(v >> 0);
  dst[1] = (uint8_t)(v >> 8);
}

// -----------------------------------------------------
// TWAI (CAN) lifecycle
// -----------------------------------------------------
static bool twaiStopAndUninstall()
{
  (void)twai_stop(); // ignore result
  const esp_err_t u = twai_driver_uninstall();
  if (u != ESP_OK) {
    Serial.printf("twai_driver_uninstall err=%d\r\n", (int)u);
    return false;
  }
  return true;
}

static bool twaiInstallAndStart()
{
  const twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);

  const twai_timing_config_t t_config = TWAI_TIMING_CFG;
  const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t e = twai_driver_install(&g_config, &t_config, &f_config);
  if (e != ESP_OK) {
    Serial.printf("TWAI install failed err=%d\r\n", (int)e);
    return false;
  }

  // Alerts used in a portable way (no TWAI_ALERT_RECOVERY_SUCCESS)
  const uint32_t alerts =
      TWAI_ALERT_BUS_OFF |
      TWAI_ALERT_ERR_PASS |
      TWAI_ALERT_RX_QUEUE_FULL |
      TWAI_ALERT_TX_FAILED |
      TWAI_ALERT_RECOVERY_IN_PROGRESS;

  (void)twai_reconfigure_alerts(alerts, nullptr);

  e = twai_start();
  if (e != ESP_OK) {
    Serial.printf("TWAI start failed err=%d\r\n", (int)e);
    (void)twai_driver_uninstall();
    return false;
  }

  Serial.println("TWAI started");
  g_twAIStarted = true;
  printTwaiStatus("START");
  return true;
}

static bool canBegin()
{
  // Ensure a clean slate if a driver instance already exists
  (void)twaiStopAndUninstall();
  return twaiInstallAndStart();
}

// If the controller is not RUNNING (bus-off, recovering, stopped), do a hard restart
static void canEnsureRunning()
{
  twai_status_info_t st;
  if (twai_get_status_info(&st) != ESP_OK) {
    g_twAIStarted = false;
    return;
  }

  if (st.state == TWAI_STATE_RUNNING) return;

  Serial.printf("TWAI not RUNNING (state=%s) -> hard restart\r\n", twaiStateToStr(st.state));
  printTwaiStatus("BEFORE_RST");

  g_twAIStarted = false;
  (void)twaiStopAndUninstall();
  (void)twaiInstallAndStart();
}

static void canPollAlerts()
{
  uint32_t alerts = 0;
  const esp_err_t e = twai_read_alerts(&alerts, 0); // non-blocking
  if (e != ESP_OK || alerts == 0) return;

  Serial.printf("ALERTS: 0x%08lX ", (unsigned long)alerts);

  if (alerts & TWAI_ALERT_BUS_OFF)             Serial.print("[BUS_OFF] ");
  if (alerts & TWAI_ALERT_ERR_PASS)            Serial.print("[ERR_PASSIVE] ");
  if (alerts & TWAI_ALERT_RX_QUEUE_FULL)       Serial.print("[RX_FULL] ");
  if (alerts & TWAI_ALERT_TX_FAILED)           Serial.print("[TX_FAILED] ");
  if (alerts & TWAI_ALERT_RECOVERY_IN_PROGRESS)Serial.print("[RECOVERING] ");

  Serial.print("\r\n");
  printTwaiStatus("ALERT");
}

static bool canTransmit(const twai_message_t& msg)
{
  canEnsureRunning();
  if (!g_twAIStarted) return false;

  const esp_err_t err = twai_transmit((twai_message_t*)&msg, pdMS_TO_TICKS(20));
  if (err != ESP_OK) {
    Serial.printf("TX failed err=%d\r\n", (int)err);
    printTwaiStatus("TXFAIL");

    // Mark as down; the next loop iteration will force a restart
    g_twAIStarted = false;
    return false;
  }

  return true;
}

// -----------------------------------------------------
// CAN Messages
// -----------------------------------------------------
static void sendHeartbeat()
{
  twai_message_t msg = {};
  msg.identifier = CAN_ID_HEARTBEAT;
  msg.extd = 0;
  msg.rtr  = 0;
  msg.data_length_code = 1;
  msg.data[0] = g_hbSeq++;
  (void)canTransmit(msg);
}

static void sendLight(uint32_t lux_x100, uint16_t full, uint16_t ir)
{
  twai_message_t msg = {};
  msg.identifier = CAN_ID_LIGHT;
  msg.extd = 0;
  msg.rtr  = 0;
  msg.data_length_code = 8;

  // Payload: [lux_x100 u32][full u16][ir u16] in Little Endian
  packU32LE(&msg.data[0], lux_x100);
  packU16LE(&msg.data[4], full);
  packU16LE(&msg.data[6], ir);

  (void)canTransmit(msg);
}

// -----------------------------------------------------
// Arduino entry points
// -----------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=== ESP32 CAN (TWAI) + TSL2591 ===");

  // I2C init
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // Sensor init
  if (!g_tsl.begin()) {
    Serial.println("TSL2591 not found!");
    while (true) delay(1000);
  }

  g_tsl.setGain(TSL2591_GAIN_MED);
  g_tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
  Serial.println("TSL2591 OK");

  // CAN init
  if (!canBegin()) {
    Serial.println("CAN init failed (will retry in loop)");
    g_twAIStarted = false;
  }

  const uint32_t now = millis();
  g_lastHbMs    = now;
  g_lastLightMs = now;
  g_lastMonMs   = now;
}

void loop()
{
  canEnsureRunning();
  canPollAlerts();

  const uint32_t now = millis();

  // Print controller status periodically
  if (now - g_lastMonMs >= MONITOR_INTERVAL_MS) {
    g_lastMonMs = now;
    printTwaiStatus("MON");
  }

  // Heartbeat
  if (now - g_lastHbMs >= HEARTBEAT_INTERVAL_MS) {
    g_lastHbMs = now;
    sendHeartbeat();
  }

  // Read sensor + transmit light data
  if (now - g_lastLightMs >= LIGHT_INTERVAL_MS) {
    g_lastLightMs = now;

    const uint32_t lum  = g_tsl.getFullLuminosity();
    const uint16_t ir   = (uint16_t)(lum >> 16);
    const uint16_t full = (uint16_t)(lum & 0xFFFF);

    float lux = g_tsl.calculateLux(full, ir);
    if (!isfinite(lux) || lux < 0.0f) lux = 0.0f;

    const uint32_t lux_x100 = (uint32_t)(lux * 100.0f);

    Serial.printf("LIGHT lux=%.2f full=%u ir=%u\r\n", lux, full, ir);
    sendLight(lux_x100, full, ir);
  }

  delay(1); // keep loop responsive
}

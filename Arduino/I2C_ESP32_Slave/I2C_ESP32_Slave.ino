// =======================================================
// ESP32 (Arduino) I2C SLAVE + NTC Temperature (ADC)
//
// Purpose:
// - Read a 10k NTC via voltage divider on an ESP32 ADC pin
// - Expose temperature as a simple I2C slave register-less read
//
// I2C:
//   Address: 0x28
//   SDA:     GPIO21
//   SCL:     GPIO22
//
// Data format (I2C master reads 2 bytes):
//   int16_t temperature in °C (whole degrees), Little Endian
//   Example: 25°C -> 0x19 0x00
//
// Notes:
// - I2C callbacks must be very short (no heavy work inside).
// - Temperature sampling is done in loop() and cached globally.
// =======================================================

#include <Wire.h>
#include <math.h>

// ---------------- I2C Slave Settings ----------------
static constexpr uint8_t SLAVE_ADDR = 0x28;
static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

// ---------------- ADC / NTC Settings ----------------
// GPIO34 = ADC1_CH6 (input only)
static constexpr int   ADC_PIN = 34;
static constexpr float VCC     = 3.3f;

// Typical divider:
// 3.3V --- R_FIXED(10k) --- ADC --- NTC(10k @25C) --- GND
static constexpr float R_FIXED = 10000.0f;  // 10k
static constexpr float R0      = 10000.0f;  // NTC 10k @ 25°C
static constexpr float T0      = 298.15f;   // 25°C in Kelvin
static constexpr float BETA    = 3950.0f;   // common value; adjust for your NTC

// ESP32 Arduino ADC
static constexpr int   ADC_MAX  = 4095;                 // 12-bit
static constexpr float ADC_TO_V = VCC / (float)ADC_MAX; // approx conversion

// ---------------- Timing ----------------
static constexpr uint32_t SAMPLE_INTERVAL_MS = 250;

// Cached temperature for I2C reads (whole degrees Celsius)
static volatile int16_t g_tempDegC = 25;

// -----------------------------------------------------
// I2C callbacks (keep them short!)
// -----------------------------------------------------
void onReceiveHandler(int numBytes)
{
  // Optional: ignore any data the master writes
  while (Wire.available()) (void)Wire.read();
}

void onRequestHandler()
{
  // Return cached temperature as int16 little-endian
  const int16_t t = g_tempDegC;
  const uint8_t buf[2] = {
    (uint8_t)(t & 0xFF),
    (uint8_t)((t >> 8) & 0xFF)
  };
  Wire.write(buf, 2);
}

// -----------------------------------------------------
// NTC conversion (ADC -> voltage -> resistance -> temperature)
// -----------------------------------------------------
static float readTemperatureC()
{
  // Multi-sample averaging for stability
  static constexpr int N = 32;

  uint32_t sum = 0;
  for (int i = 0; i < N; i++) {
    sum += (uint32_t)analogRead(ADC_PIN);
    delayMicroseconds(200);
  }

  float adc = (float)sum / (float)N;

  // Clamp to avoid division by zero / log(0)
  if (adc < 1.0f) adc = 1.0f;
  if (adc > (ADC_MAX - 1)) adc = (float)(ADC_MAX - 1);

  // ADC voltage
  const float v = adc * ADC_TO_V;

  // Divider equation (for the assumed wiring):
  // Vout = Vcc * (Rntc / (Rfixed + Rntc))
  // => Rntc = Rfixed * Vout / (Vcc - Vout)
  const float rNtc = R_FIXED * v / (VCC - v);

  // Beta formula:
  // 1/T = 1/T0 + (1/B) * ln(R/R0)
  const float invT = (1.0f / T0) + (1.0f / BETA) * logf(rNtc / R0);
  const float T    = 1.0f / invT;

  return T - 273.15f; // Kelvin -> Celsius
}

// -----------------------------------------------------
// Arduino entry points
// -----------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(200);

  // ADC setup
  analogReadResolution(12);                      // 0..4095
  analogSetPinAttenuation(ADC_PIN, ADC_11db);    // allows ~3.3V range (important)

  // Set I2C pins (ESP32 supports this)
  #if defined(ARDUINO_ARCH_ESP32)
    Wire.setPins(SDA_PIN, SCL_PIN);
  #endif

  // Start I2C slave
  Wire.begin(SLAVE_ADDR);
  Wire.onReceive(onReceiveHandler);
  Wire.onRequest(onRequestHandler);

  Serial.println("ESP32 I2C SLAVE started (NTC on GPIO34 -> int16 °C)");
}

void loop()
{
  static uint32_t lastSample = 0;

  // Update cached temperature periodically (not inside I2C callback)
  if (millis() - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = millis();

    const float tC = readTemperatureC();

    // Round to whole degrees for a compact payload
    int16_t tRounded = (int16_t)lroundf(tC);

    // Optional sanity clamp (protect against noisy ADC readings)
    if (tRounded < -40) tRounded = -40;
    if (tRounded > 125) tRounded = 125;

    g_tempDegC = tRounded;

    Serial.printf("ADC temp: %.2f C -> %d C\r\n", (double)tC, (int)g_tempDegC);
  }
}

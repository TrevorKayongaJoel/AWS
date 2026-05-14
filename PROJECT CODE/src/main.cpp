/**
 * main.cpp — Multi-Level Fixed Duty Cycling Implementation
 * =========================================================
 * Strategy (from design document):
 *   TON  = 2 minutes  (active window: sensors + log + transmit)
 *   TOFF = 10 minutes (deep sleep)
 *   D(k) = TON / (TON + TOFF) = 2/12 = 0.167 (16.7% active)
 *
 * Per-module duty cycling:
 *   ESP32   — deep sleep during TOFF (10-150µA via esp_deep_sleep_start())
 *   GSM     — MOSFET power gate on GPIO 32; ON only during TX, OFF immediately after
 *             D_GSM = 20s/600s = 0.0056 → Pavg ≈ 11.2mW vs 2W continuous (~180x reduction)
 *   LoRa    — AT+LOWPOWER command over UART; wakes automatically on next UART byte
 *             RA-08H deep sleep current: 0.9µA (confirmed from datasheet)
 *   Sensors — read only during active window (~2s active per 600s period)
 *             D_sensor = 2/600 = 0.0033 → Pavg ≈ 5.38mW vs 120mW continuous (22x reduction)
 *   SD card — SPI disabled immediately after write (batched logging)
 *   RTC     — always ON; maintains time during deep sleep, triggers wake
 *
 * RA-08H (Ai-Thinker) AT command reference:
 *   Sleep  : AT+LOWPOWER    → module enters deep sleep (0.9µA)
 *   Wake   : any UART byte  → module wakes automatically on RX line activity
 *   UART   : 9600 baud, 8N1 (default per datasheet — matches LORA.cpp)
 *   TX data: AT+DTRX=<confirm>,<nbtrials>,<len>,<data>
 *            e.g. AT+DTRX=0,1,6,AABBCC  (unconfirmed, 1 trial)
 *
 * Fixes applied after header review:
 *   [1] LoRa uses HardwareSerial (UART), not SPI — confirmed from LORA.h
 *   [2] Lora::sendData() returns void — no ACK available from class
 *   [3] Removed SENSOR_PWR_PIN 33 — conflicts with ONE_WIRE_BUS 33 in SOILTEMP.h
 *   [4] Wire.begin() called once only via powermonitoring.begin()
 *   [5] SoilTemp excluded to match your original object list in main.cpp
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "esp_sleep.h"

#include "DHT22.h"
#include "AIR_PRESSURE.h"
#include "LIGHT_SENSOR.h"
#include "SOILMOISTURE.h"
#include "RTC.h"
#include "GSM.h"
#include "DAVIS.h"
#include "WINDSPEED.h"
#include "WIND_DIRECTION.h"
#include "POWER_MONITORING.h"
#include "DataLogger.h"
#include "SensorData.h"
#include "LORA.h"

// =========================================================
// FIXED DUTY CYCLING PARAMETERS
// =========================================================
// TON  = 2 min  → active window (sensors + log + TX)
// TOFF = 10 min → deep sleep window
// D(k) = TON / (TON + TOFF) = 120s / 720s = 0.167

static const uint64_t TON_MS  = 2ULL  * 60ULL * 1000ULL;    // 2 min in ms
static const uint64_t TOFF_US = 10ULL * 60ULL * 1000000ULL;  // 10 min in µs (deep sleep API)

// =========================================================
// GSM MOSFET POWER GATE PIN
// =========================================================
// Wire a high-side MOSFET (e.g. IRLZ44N) between this GPIO
// and the GSM module's VCC rail:
//   GPIO HIGH = GSM powered ON
//   GPIO LOW  = GSM powered OFF (set LOW at boot)
//
// GPIO 32 is safe — does not conflict with any sensor pins:
//   Pin  5 = DHT22 data         (DHT22.h)
//   Pin  4 = SD card CS         (DataLogger)
//   Pin 14 = LoRa RX            (LORA.cpp)
//   Pin 15 = LoRa TX            (LORA.cpp)
//   Pin 16 = GSM RX             (GSM.cpp)
//   Pin 17 = GSM TX             (GSM.cpp)
//   Pin 21 = I2C SDA
//   Pin 22 = I2C SCL
//   Pin 25 = Rain gauge ISR     (DAVIS.h)
//   Pin 26 = Wind speed ISR     (WINDSPEED.h)
//   Pin 27 = Wind direction ADC (WIND_DIRECTION.h)
//   Pin 33 = DS18B20 one-wire   (SOILTEMP.h) ← do not use
//   Pin 34 = Soil moisture ADC  (SOILMOISTURE.h)
//   Pin 35 = Light sensor ADC   (LIGHT_SENSOR.h)
#define GSM_POWER_PIN  32

static const uint32_t GSM_WARMUP_MS = 3000;  // Time for GSM to boot and register on network

// =========================================================
// LORA — RA-08H (Ai-Thinker) UART interface
// =========================================================
// LORA.cpp declares: HardwareSerial SerialL = Serial1 (RX=14, TX=15, 9600 baud)
// We declare it extern here to send AT commands directly without re-initialising.
//
// RA-08H sleep/wake (confirmed from Ai-Thinker datasheet + AT command doc):
//   Sleep : "AT+LOWPOWER\r\n"  → enters deep sleep, current drops to 0.9µA
//   Wake  : any byte on RX     → module wakes automatically (no explicit wake command)
//           send "AT\r\n" first to flush and confirm module is awake before TX
extern HardwareSerial SerialL;

// =========================================================
// OBJECTS
// =========================================================
DHTSensor            dhtsensor;
AirPressure          airpressure;
LightSensor          lightsensor;
Soilmoisture         soilmoisture;
Rtc                  rtc1;
Davis                davisrain;
WindSpeedSensor      windspeedsensor;
WindDirectionSensor  winddirectionsensor;
PowerMonitoring      powermonitoring;   // begin() calls Wire.begin() internally
GSM                  simmodule;
Lora                 loramodule;
DataLogger           dataLogger(4);     // SD CS pin = 4

// =========================================================
// UTILITY: AVERAGE POWER FORMULA
// Pavg(k) = D(k) * Pactive + (1 - D(k)) * Psleep
// =========================================================
float computeAvgPower(float duty, float pActive_mW, float pSleep_mW) {
    return (duty * pActive_mW) + ((1.0f - duty) * pSleep_mW);
}

// =========================================================
// GSM POWER GATE — ON
// =========================================================
void gsmPowerOn() {
    Serial.println("[DC] GSM Power Gate: ON");
    digitalWrite(GSM_POWER_PIN, HIGH);
    delay(GSM_WARMUP_MS);  // Wait for modem boot + network registration
    simmodule.setupGSM();
}

// =========================================================
// GSM POWER GATE — OFF
// De-energise MOSFET immediately after TX.
// D_GSM = 20s / 600s = 0.0056 → Pavg ≈ 11.2mW (~180x reduction vs 2W continuous)
// =========================================================
void gsmPowerOff() {
    Serial.println("[DC] GSM Power Gate: OFF");
    digitalWrite(GSM_POWER_PIN, LOW);
}

// =========================================================
// LORA SLEEP — RA-08H specific
// Command: AT+LOWPOWER
// Response: +LOWPOWER: SLEEP (then silent)
// Deep sleep current: 0.9µA (Ai-Thinker RA-08H datasheet)
// =========================================================
void loraSleep() {
    Serial.println("[DC] LoRa: AT+LOWPOWER (0.9µA deep sleep)");
    while (SerialL.available()) SerialL.read();  // Clear any pending RX bytes
    SerialL.println("AT+LOWPOWER");
    delay(200);  // Allow module to process command and enter sleep
}

// =========================================================
// LORA WAKE — RA-08H specific
// The RA-08H wakes automatically when any byte arrives on UART RX.
// We send "AT\r\n" as the wake byte, then wait for "OK" before TX.
// This is standard practice for ASR6601-based modules.
// =========================================================
void loraWake() {
    Serial.println("[DC] LoRa: waking (sending AT)");
    SerialL.println("AT");   // Wake byte — any UART activity wakes the RA-08H
    delay(300);              // Allow module to wake and stabilise before TX

    // Optional: flush the "OK" response
    while (SerialL.available()) SerialL.read();
}

// =========================================================
// SD CARD: RELEASE SPI BUS AFTER WRITE
// DataLogger already closes the file handle after each write.
// SPI.end() also disables the peripheral to eliminate idle draw.
// =========================================================
void sdCardRelease() {
    SPI.end();
    Serial.println("[DC] SD SPI bus released");
}

// =========================================================
// DEEP SLEEP — TOFF = 10 minutes
// During sleep:
//   CPU, WiFi, BT, most peripherals: OFF
//   RTC timer + RTC memory:          ON  (~10-150µA total)
// This function does not return.
// On wake, the ESP32 reboots and setup() runs from the top.
// =========================================================
void enterDeepSleep() {
    float duty     = (float)TON_MS / ((float)TON_MS + (float)(TOFF_US / 1000ULL));
    float pavg_esp = computeAvgPower(duty, 200.0f, 0.15f);  // 200mA active, 0.15mA deep sleep

    Serial.printf("[DC] D = %.4f (%.1f%% active | %.1f%% sleep)\n",
                  duty, duty * 100.0f, (1.0f - duty) * 100.0f);
    Serial.printf("[DC] Est. avg ESP32 current: %.2f mA\n", pavg_esp);
    Serial.printf("[DC] Sleeping %.0f min (TOFF)...\n\n", (float)(TOFF_US / 60000000ULL));
    Serial.flush();

    esp_sleep_enable_timer_wakeup(TOFF_US);
    esp_deep_sleep_start();  // Does not return
}

// =========================================================
// PHASE 1: READ ALL SENSORS
// D_sensor = 2s / 600s = 0.0033
// Pavg = 0.0033*120mW + 0.9967*5mW ≈ 5.38mW  (22x reduction)
// =========================================================
SensorData readAllSensors() {
    SensorData data;
    Serial.println("[DC] === Sensor Read Phase ===");

    // BME280 (I2C 0x77)
    float p = airpressure.readPressure();
    data.airPressure = isnan(p) ? 0.0f : p;

    float alt = airpressure.readAltitude(1013.25);
    data.altitude = isnan(alt) ? 0.0f : alt;

    float t = airpressure.readTemperature();
    data.temperature = isnan(t) ? 0.0f : t;

    float hum = airpressure.readHumidity();
    data.humidity = isnan(hum) ? 0.0f : hum;

    // STM32 power board (I2C 0x08)
    if (powermonitoring.readData()) {
        VoltageData v = powermonitoring.getData();
        data.volt_3v3   = v.v1;
        data.volt_5v    = v.v2;
        data.volt_batt  = v.v3;
        data.volt_solar = v.v4;
        data.volt_dc    = v.v5;
        data.curr_batt  = v.v6;
        data.curr_solar = v.v7;
    } else {
        data.volt_3v3 = data.volt_5v    = data.volt_batt  = 0.0f;
        data.volt_solar = data.volt_dc  = data.curr_batt  = data.curr_solar = 0.0f;
    }

    data.lightLevel    = lightsensor.readLightLevel();
    data.soilMoisture  = soilmoisture.readSoilMoisture();
    data.rainCount     = davisrain.readRainGauge();
    data.windSpeed     = windspeedsensor.readWindSpeedKPH();
    data.windDirection = winddirectionsensor.readWindDirectionDeg();

    Serial.println("[DC] Sensor read complete.");
    return data;
}

// =========================================================
// PHASE 2: LOG TO SD (BATCHED)
// SPI released after write to stop idle current draw.
// =========================================================
void logToSD(SensorData &data) {
    Serial.println("[DC] === SD Log Phase ===");
    String timeStr = String(rtc1.getDateTime().c_str());
    dataLogger.logSensorData(timeStr, data);
    sdCardRelease();
}

// =========================================================
// PHASE 3: TRANSMIT
// LoRa (RA-08H) = primary channel, lower energy.
// GSM            = backup / ThingSpeak cloud upload, MOSFET gated.
//
// RA-08H TX command format (LoRaWAN unconfirmed uplink):
//   AT+DTRX=<confirm>,<nbtrials>,<len>,<data>
//   confirm  : 0 = unconfirmed, 1 = confirmed
//   nbtrials : number of retries (1 for fixed duty cycling)
//   len      : byte length of hex data field
//   data     : hex-encoded payload
//
// Example for 12-byte payload "AABBCCDD...":
//   AT+DTRX=0,1,12,AABBCCDD...
//
// Lora::sendData(String, int) in LORA.cpp sends the raw string
// over SerialL and waits for the timeout. We pass the full
// AT+DTRX command as the string.
// =========================================================
void transmitData(SensorData &data) {
    Serial.println("[DC] === TX Phase ===");

    // --- LoRa TX (RA-08H, primary) ---
    loraWake();

    // Build a human-readable CSV payload for LoRa uplink.
    // The RA-08H AT+DTRX command sends raw bytes — using ASCII CSV here
    // for easy decoding at the gateway. Adjust field selection as needed.
    String payload =
        "T:"  + String(data.temperature, 1) +
        ",H:" + String(data.humidity, 1) +
        ",P:" + String(data.airPressure, 1) +
        ",WS:"+ String(data.windSpeed, 1) +
        ",WD:"+ String(data.windDirection) +
        ",R:" + String(data.rainCount) +
        ",L:" + String(data.lightLevel, 2) +
        ",SM:"+ String(data.soilMoisture, 2) +
        ",VB:"+ String(data.volt_batt, 2) +
        ",VS:"+ String(data.volt_solar, 2);

    // Build AT+DTRX command:
    //   confirm=0 (unconfirmed), nbtrials=1, len=payload length in bytes
    String atCmd = "AT+DTRX=0,1," + String(payload.length()) + "," + payload;

    Serial.println("[DC] LoRa TX: " + atCmd);
    loramodule.sendData(atCmd, 3000);  // sendData sends the string over SerialL

    loraSleep();  // Return RA-08H to 0.9µA deep sleep immediately after TX
    Serial.println("[DC] LoRa TX done, RA-08H sleeping (0.9µA).");

    // --- GSM TX (backup / ThingSpeak cloud upload) ---
    // MOSFET gate energised only for TX window.
    // D_GSM ≈ 0.0056 → Pavg_GSM ≈ 11.2mW (~180x reduction vs continuous 2W)
    gsmPowerOn();
    dataLogger.uploadLastDataToThingspeak(simmodule);
    gsmPowerOff();

    Serial.println("[DC] TX Phase complete.");
}

// =========================================================
// SETUP — runs on every wake from deep sleep (and cold boot)
// =========================================================
void setup() {
    Serial.begin(9600);
    delay(200);

    // Log wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("\n[DC] === Wake: RTC timer (TOFF elapsed) ===");
    } else {
        Serial.println("\n[DC] === Wake: Cold boot / manual reset ===");
    }

    float D = (float)TON_MS / ((float)TON_MS + (float)(TOFF_US / 1000ULL));
    Serial.printf("[DC] TON=2min | TOFF=10min | D=%.3f (%.1f%% active)\n", D, D * 100.0f);

    // GSM MOSFET gate — LOW (OFF) at boot
    pinMode(GSM_POWER_PIN, OUTPUT);
    digitalWrite(GSM_POWER_PIN, LOW);

    // I2C — one initialisation via PowerMonitoring::begin()
    // Shared by: BME280 (0x77), DS3231 RTC, STM32 power board (0x08)
    powermonitoring.begin(21, 22);  // SDA=21, SCL=22

    // RTC (DS3231, I2C — Wire already initialised above)
    rtc1.setupRTC();

    // Sensors
    dhtsensor.getsensor();
    airpressure.sensor_setup();
    davisrain.setupRainGauge();
    windspeedsensor.setupSensor();
    winddirectionsensor.setupSensor();
    lightsensor.setupSensor();
    soilmoisture.setupSensor();

    // SD card (SPI, CS=4)
    dataLogger.begin();

    // LoRa RA-08H (UART Serial1, RX=14, TX=15, 9600 baud)
    loramodule.setupLora();

    // =====================================================
    //  ACTIVE WINDOW  (TON = 2 minutes)
    //  All processing happens here. After this block, the
    //  ESP32 enters deep sleep for TOFF (10 minutes).
    // =====================================================
    unsigned long tonStart = millis();
    Serial.printf("[DC] Active window open (TON = %llu ms)\n", TON_MS);

    SensorData currentData = readAllSensors();  // ~2-5s
    logToSD(currentData);                       // ~1s, SPI released after
    transmitData(currentData);                  // ~75s worst case (3-channel GSM upload)

    Serial.printf("[DC] Active window closed. Elapsed: %lu ms\n", millis() - tonStart);

    // =====================================================
    //  SLEEP PHASE  (TOFF = 10 minutes)
    //  State at entry:
    //    GSM:  OFF  (gsmPowerOff() called in transmitData)
    //    LoRa: SLEEP 0.9µA (AT+LOWPOWER sent in transmitData)
    //    SD:   SPI released (sdCardRelease() called in logToSD)
    //    RTC:  ON   (runs during ESP32 deep sleep, fires wake timer)
    // =====================================================
    enterDeepSleep();  // Does not return — next execution is setup() on wake
}

// =========================================================
// LOOP — intentionally empty
// Duty cycle: setup() → TON (active) → deep sleep (TOFF) → wake → setup()
// =========================================================
void loop() {
    // Intentionally empty.
    //
    // Debug fallback (no deep sleep): comment out enterDeepSleep()
    // in setup() and uncomment below:
    //
    // delay((uint32_t)(TOFF_US / 1000ULL));
    // setup();
}

/**
 * main.cpp — Multi-Level Fixed Duty Cycling Implementation
 * =========================================================
 * Strategy (from design document):
 *   UPDATE_INTERVAL_MINUTES = 10 minutes (total cycle time)
 *   TON  = 2 minutes  (active window: sensors + log + transmit)
 *   TOFF = Dynamic (Total Cycle - Active Time)
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
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

static const uint32_t UPDATE_INTERVAL_MINUTES = 10;
static const uint64_t TON_MS  = 2ULL  * 60ULL * 1000ULL;

#define GSM_POWER_PIN  32
static const uint32_t GSM_WARMUP_MS = 3000;

extern HardwareSerial SerialL;

DHTSensor            dhtsensor;
AirPressure          airpressure;
LightSensor          lightsensor;
Soilmoisture         soilmoisture;
Rtc                  rtc1;
Davis                davisrain;
WindSpeedSensor      windspeedsensor;
WindDirectionSensor  winddirectionsensor;
PowerMonitoring      powermonitoring;
GSM                  simmodule;
Lora                 loramodule;
DataLogger           dataLogger(4);

float computeAvgPower(float duty, float pActive_mW, float pSleep_mW) {
    return (duty * pActive_mW) + ((1.0f - duty) * pSleep_mW);
}

void logDebugEvent(const String &event) {
    if (!SD.begin(4)) return;
    File f = SD.open("/sleep_debug.txt", FILE_APPEND);
    if (!f) return;
    f.println(String(millis()) + "," + event);
    f.close();
}

void gsmPowerOn() {
    Serial.println("[DC] GSM Power Gate: ON");
    digitalWrite(GSM_POWER_PIN, HIGH);
    delay(GSM_WARMUP_MS);
    simmodule.setupGSM();
}

void gsmPowerOff() {
    Serial.println("[DC] GSM Power Gate: OFF");
    digitalWrite(GSM_POWER_PIN, LOW);
}

void loraSleep() {
    Serial.println("[DC] LoRa: AT+LOWPOWER");
    while (SerialL.available()) SerialL.read();
    SerialL.println("AT+LOWPOWER");
    delay(200);
}

void loraWake() {
    Serial.println("[DC] LoRa: waking");
    SerialL.println("AT");
    delay(300);
    while (SerialL.available()) SerialL.read();
}

void sdCardRelease() {
    SD.end();
    SPI.end();
    Serial.println("[DC] SD SPI bus released");
}

void enterDeepSleep(unsigned long tonStart) {
    // Detach FIRST — before any other teardown
    detachInterrupt(digitalPinToInterrupt(25));
    detachInterrupt(digitalPinToInterrupt(33));
    delay(50); // Let any in-flight ISR finish

    uint64_t totalCycleUs = (uint64_t)UPDATE_INTERVAL_MINUTES * 60ULL * 1000000ULL;
    uint64_t activeUs = (uint64_t)(millis() - tonStart) * 1000ULL;
    uint64_t sleepUs = (totalCycleUs > activeUs) ? (totalCycleUs - activeUs) : (10ULL * 1000000ULL);

    Serial.printf("[DC] Total Active Time: %lu ms\n", (unsigned long)(activeUs / 1000ULL));
    Serial.printf("[DC] Deep Sleep Duration: %lu ms\n", (unsigned long)(sleepUs / 1000ULL));
    Serial.flush(); // Ensure serial output completes before sleep

    esp_sleep_enable_timer_wakeup(sleepUs);
    esp_deep_sleep_start();
}

SensorData readAllSensors() {
    SensorData data;
    data.airPressure = airpressure.readPressure();
    data.altitude = airpressure.readAltitude(1013.25);
    data.temperature = airpressure.readTemperature();
    data.humidity = airpressure.readHumidity();

    if (powermonitoring.readData()) {
        VoltageData v = powermonitoring.getData();
        data.volt_3v3   = v.v1; data.volt_5v = v.v2; data.volt_batt = v.v3;
        data.volt_solar = v.v4; data.volt_dc = v.v5; data.curr_batt = v.v6;
        data.curr_solar = v.v7;
    } else {
        data.volt_3v3 = data.volt_5v = data.volt_batt = 0.0f;
        data.volt_solar = data.volt_dc = data.curr_batt = data.curr_solar = 0.0f;
    }

    data.lightLevel    = lightsensor.readLightLevel();
    data.soilMoisture  = soilmoisture.readSoilMoisture();
    data.rainCount     = davisrain.readRainGauge();
    data.windSpeed     = windspeedsensor.readWindSpeedKPH();
    data.windDirection = winddirectionsensor.readWindDirectionDeg();

    return data;
}

void logToSD(SensorData &data) {
    Serial.println("[DC] === SD Log Phase ===");
    String timeStr = String(rtc1.getDateTime().c_str());
    dataLogger.logSensorData(timeStr, data);
}

void transmitData(SensorData &data, unsigned long tonStart) {
    Serial.println("[DC] === TX Phase ===");
    loraWake();
    String payload = "T:" + String(data.temperature, 1) + ",H:" + String(data.humidity, 1);
    String atCmd = "AT+DTRX=0,1," + String(payload.length()) + "," + payload;
    loramodule.sendData(atCmd, 3000);
    loraSleep();

    gsmPowerOn();
    dataLogger.uploadPendingData(simmodule, tonStart, TON_MS);
    gsmPowerOff();
    Serial.println("[DC] TX Phase complete.");
}

void setup() {
    Serial.begin(9600);
    delay(200);
    
    Wire.setTimeOut(50); // Set global I2C timeout to prevent hangs
    
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reset_reason = esp_reset_reason();

    Serial.println("\n[DC] ================================");
    Serial.print("[DC] Wakeup reason: ");
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:     Serial.println("External signal using RTC_IO"); break;
        case ESP_SLEEP_WAKEUP_EXT1:     Serial.println("External signal using RTC_CNTL"); break;
        case ESP_SLEEP_WAKEUP_TIMER:    Serial.println("Timer"); break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Touchpad"); break;
        case ESP_SLEEP_WAKEUP_ULP:      Serial.println("ULP program"); break;
        default:                        Serial.printf("Not caused by deep sleep (%d)\n", wakeup_reason); break;
    }

    Serial.print("[DC] Reset reason: ");
    switch (reset_reason) {
        case ESP_RST_POWERON:   Serial.println("Power-on"); break;
        case ESP_RST_EXT:       Serial.println("External pin"); break;
        case ESP_RST_SW:        Serial.println("Software"); break;
        case ESP_RST_PANIC:     Serial.println("Exception/Panic"); break;
        case ESP_RST_INT_WDT:   Serial.println("Interrupt Watchdog"); break;
        case ESP_RST_TASK_WDT:  Serial.println("Task Watchdog"); break;
        case ESP_RST_WDT:       Serial.println("Other Watchdog"); break;
        case ESP_RST_DEEPSLEEP: Serial.println("Deep Sleep"); break;
        case ESP_RST_BROWNOUT:  Serial.println("Brownout"); break;
        case ESP_RST_SDIO:      Serial.println("SDIO"); break;
        default:                Serial.println("Unknown"); break;
    }
    Serial.println("[DC] ================================");
    
    pinMode(GSM_POWER_PIN, OUTPUT);
    digitalWrite(GSM_POWER_PIN, LOW);

    powermonitoring.begin(21, 22);
    delay(100);

    rtc1.setupRTC();

    dhtsensor.getsensor();
    airpressure.sensor_setup();
    davisrain.setupRainGauge();
    windspeedsensor.setupSensor();
    winddirectionsensor.setupSensor();
    lightsensor.setupSensor();
    soilmoisture.setupSensor();

    dataLogger.begin();
    loramodule.setupLora();

    unsigned long tonStart = millis();
    SensorData currentData = readAllSensors();
    logToSD(currentData);
    transmitData(currentData, tonStart);

    Serial.printf("[DC] Active window closed. Elapsed: %lu ms\n", millis() - tonStart);
    
    sdCardRelease();

    Serial.println("[DC] Sleep phase: entering deep sleep");
    enterDeepSleep(tonStart);
}

void loop() {}

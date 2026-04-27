#ifndef POWER_MONITORING_H
#define POWER_MONITORING_H

#define POWER_BOARD_ADDR 0x08

#include <Arduino.h>
#include <Wire.h>

// Data structure matching the Slave device
struct VoltageData {
    float v1; // 3.3V Rail
    float v2; // 5V Rail
    float v3; // Battery
    float v4; // Solar
    float v5; // DC In
    float v6; // Battery current (ACS712)
    float v7; // Solar current (ACS712)
};

class PowerMonitoring {
  private:
    uint8_t _slaveAddress;
    uint8_t _payloadSize;
    VoltageData _storedData; 

  public:
    // Constructor
    PowerMonitoring();

    // Initialization (Defaults to ESP32 pins 21/22)
    void begin(int sdaPin = 21, int sclPin = 22);

    // Reads data from the power board
    bool readData();

    // Returns the latest readings
    VoltageData getData();
};

#endif
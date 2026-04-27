#include "POWER_MONITORING.h"


// Constructor
PowerMonitoring::PowerMonitoring() {
  _slaveAddress = POWER_BOARD_ADDR;
  _payloadSize = sizeof(VoltageData); // 28 bytes (7 floats)
}

// Setup I2C
void PowerMonitoring::begin(int sdaPin, int sclPin) {
  Wire.begin(sdaPin, sclPin);
}

// Read logic
bool PowerMonitoring::readData() {
  uint8_t bytesReceived = Wire.requestFrom(_slaveAddress, _payloadSize);

  if (bytesReceived == _payloadSize) {
    // Read directly into struct memory
    Wire.readBytes((uint8_t*)&_storedData, _payloadSize);
    return true;
  } 
  else {
    // Flush buffer on error
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
}

// Getter
VoltageData PowerMonitoring::getData() {
  return _storedData;
}
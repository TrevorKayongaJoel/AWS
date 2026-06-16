#include "LORA.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#define RX 14
#define TX 15
HardwareSerial SerialL = Serial1; // Assuming Serial1 is used for LoRa
Lora::Lora() {}
void Lora::setupLora() {
    // Initialize LoRa module here
    SerialL.begin(9600,SERIAL_8N1, RX, TX); // Set baud rate for LoRa module
 if(SerialL) {
        Serial.println("LoRa module serial initialized.");
    } else {
        Serial.println("Failed to initialize LoRa module serial.");
    }
}

void Lora::sendData(const String& command, int timeout) {
    Serial.print("Sending: ");
    Serial.println(command);
  
    // Clear buffer
    while(SerialL.available()) SerialL.read();
  
    // Send Command
    SerialL.println(command);

    // Wait for response
    unsigned long start = millis();
    while (millis() - start < (unsigned long)timeout) {
        while (SerialL.available()) {
            char c = SerialL.read();
            Serial.write(c);
        }
        delay(1); // Feed the watchdog
    }
    Serial.println("\n-----------------------");
}
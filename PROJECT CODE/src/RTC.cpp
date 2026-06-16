#include "RTC.h"
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

Rtc::Rtc() : date_time_(""), rtcFound(false) {}

void Rtc::setupRTC() {
    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        rtcFound = false;
    } else {
        rtcFound = true;
    }

    if (rtcFound && rtc.lostPower()) {
        Serial.println("RTC lost power, let's set the time!");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
}

bool Rtc::syncWithNTP(const char* ntpServer, long gmtOffsetSec, int daylightOffsetSec) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot sync NTP");
        return false;
    }

    // Configure time via ESP32 internal clock
    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);

    struct tm timeinfo;
    Serial.print("Waiting for NTP time sync...");
    
    // Try to get the time 10 times
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
        Serial.print(".");
        delay(500);
        retry++;
    }

    if (retry >= 10) {
        Serial.println("\nFailed to get NTP time");
        return false;
    }

    // Successfully got NTP time, now update the DS3231 RTC hardware
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, 
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));

    Serial.println("\nRTC synchronized with NTP successfully.");
    return true;
}

void Rtc::printDateTime() {
    std::string currentTime = getDateTime();
    Serial.println(currentTime.c_str());
}

std::string Rtc::getDateTime() {
    if (!rtcFound) {
        return "2000-01-01 00:00:00";
    }
    DateTime now = rtc.now();
    
    char buf[100];
    // Formats: Sunday, 2023-10-27 14:30:05
    snprintf(buf, sizeof(buf), "%s, %04d-%02d-%02d %02d:%02d:%02d", 
             daysOfTheWeek[now.dayOfTheWeek()],
             now.year(), now.month(), now.day(), 
             now.hour(), now.minute(), now.second());

    date_time_ = std::string(buf);
    return date_time_;
}

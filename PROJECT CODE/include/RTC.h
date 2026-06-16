#ifndef RTC_H
#define RTC_H

#include <Arduino.h>
#include <RTClib.h>
#include <string>

class Rtc {
public:
    Rtc();
    void setupRTC();
    bool syncWithNTP(const char* ntpServer, long gmtOffsetSec, int daylightOffsetSec);
    std::string getDateTime();
    void printDateTime(); 

private:
    RTC_DS3231 rtc;
    std::string date_time_;
    bool rtcFound;
};

#endif
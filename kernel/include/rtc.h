#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PORT_RTC_COMMAND 0x70
#define PORT_RTC_DATA    0x71

#define RTC_REGISTER_SECONDS  0x00
#define RTC_REGISTER_MINUTES  0x02
#define RTC_REGISTER_HOURS    0x04
#define RTC_REGISTER_DAY      0x07
#define RTC_REGISTER_MONTH    0x08
#define RTC_REGISTER_YEAR     0x09
#define RTC_REGISTER_STATUS_A 0x0A
#define RTC_REGISTER_STATUS_B 0x0B

#define RTC_UPDATE_IN_PROGRESS (1 << 7)
#define RTC_DATA_MODE          (1 << 2)
#define RTC_24_HOUR            (1 << 1)
#define RTC_HOUR_PM            (1 << 7)

struct rtc_time {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

void rtc_read(struct rtc_time *t);
bool rtc_valid(const struct rtc_time *t);

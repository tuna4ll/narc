#include "rtc.h"
#include "io.h"

static uint8_t rtc_read_register(uint8_t reg) {
    outb(PORT_RTC_COMMAND, reg);
    return inb(PORT_RTC_DATA);
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (v & 0x0F) + (v >> 4) * 10;
}

static void rtc_snapshot(uint8_t raw[6]) {
    static const uint8_t regs[6] = {
        RTC_REGISTER_SECONDS, RTC_REGISTER_MINUTES, RTC_REGISTER_HOURS,
        RTC_REGISTER_DAY, RTC_REGISTER_MONTH, RTC_REGISTER_YEAR,
    };

    while (rtc_read_register(RTC_REGISTER_STATUS_A) & RTC_UPDATE_IN_PROGRESS)
        ;
    for (int i = 0; i < 6; i++)
        raw[i] = rtc_read_register(regs[i]);
}

void rtc_read(struct rtc_time *t) {
    uint8_t a[6];
    uint8_t b[6];
    uint64_t flags;

    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");

    rtc_snapshot(b);
    do {
        for (int i = 0; i < 6; i++)
            a[i] = b[i];
        rtc_snapshot(b);
    } while (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] ||
             a[3] != b[3] || a[4] != b[4] || a[5] != b[5]);

    uint8_t status_b = rtc_read_register(RTC_REGISTER_STATUS_B);

    __asm__ volatile ("push %0; popfq" : : "r"(flags) : "memory", "cc");

    bool pm = b[2] & RTC_HOUR_PM;
    b[2] &= ~RTC_HOUR_PM;

    if (!(status_b & RTC_DATA_MODE))
        for (int i = 0; i < 6; i++)
            b[i] = bcd_to_bin(b[i]);

    if (!(status_b & RTC_24_HOUR)) {
        if (b[2] == 12)
            b[2] = 0;
        if (pm)
            b[2] += 12;
    }

    t->second = b[0];
    t->minute = b[1];
    t->hour = b[2];
    t->day = b[3];
    t->month = b[4];
    t->year = 2000 + b[5];
}

bool rtc_valid(const struct rtc_time *t) {
    return t->second < 60 && t->minute < 60 && t->hour < 24 &&
           t->day >= 1 && t->day <= 31 && t->month >= 1 && t->month <= 12 &&
           t->year >= 2000 && t->year <= 2099;
}

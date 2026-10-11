#include <kernel/arch.h>
#include <kernel/io.h>
#include <kernel/string.h>
#include <kernel/time.h>

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static unsigned from_bcd(uint8_t value, int bcd) {
    return bcd ? (value & 0x0f) + (value >> 4) * 10 : value;
}

static int64_t days_from_civil(int64_t year, unsigned month, unsigned day) {
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t arch_rtc_seconds(void) {
    uint8_t regs[7], again[7];
    static const uint8_t index[7] = { 0x00, 0x02, 0x04, 0x07, 0x08, 0x09, 0x32 };
    do {
        while (cmos_read(0x0a) & 0x80) arch_cpu_relax();
        for (int i = 0; i < 7; i++) regs[i] = cmos_read(index[i]);
        for (int i = 0; i < 7; i++) again[i] = cmos_read(index[i]);
    } while (memcmp(regs, again, sizeof(regs)));
    uint8_t status = cmos_read(0x0b);
    int bcd = !(status & 0x04);
    unsigned hour = from_bcd(regs[2] & 0x7f, bcd);
    if (!(status & 0x02) && (regs[2] & 0x80)) hour = (hour % 12) + 12;
    unsigned century = regs[6] ? from_bcd(regs[6], bcd) : 20;
    int64_t year = (int64_t)century * 100 + from_bcd(regs[5], bcd);
    int64_t days = days_from_civil(year, from_bcd(regs[4], bcd), from_bcd(regs[3], bcd));
    return days * 86400 + hour * 3600 + from_bcd(regs[1], bcd) * 60 + from_bcd(regs[0], bcd);
}

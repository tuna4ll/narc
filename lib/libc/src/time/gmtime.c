#include <time.h>

static int64_t floor_div(int64_t value, int64_t divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

struct tm *gmtime_r(const time_t *value, struct tm *result) {
    int64_t days = floor_div(*value, 86400);
    int64_t seconds = *value - days * 86400;
    result->tm_hour = (int)(seconds / 3600);
    result->tm_min = (int)(seconds / 60 % 60);
    result->tm_sec = (int)(seconds % 60);
    result->tm_wday = (int)((days % 7 + 11) % 7);

    int64_t z = days + 719468;
    int64_t era = floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t day = doy - (153 * mp + 2) / 5 + 1;
    int64_t month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = yoe + era * 400 + (month <= 2);
    result->tm_mday = (int)day;
    result->tm_mon = (int)month - 1;
    result->tm_year = (int)(year - 1900);
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    static const int start[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    result->tm_yday = start[month - 1] + (int)day - 1 + (leap && month > 2);
    result->tm_isdst = 0;
    return result;
}

struct tm *gmtime(const time_t *value) {
    static struct tm result;
    return gmtime_r(value, &result);
}

struct tm *localtime_r(const time_t *value, struct tm *result) {
    return gmtime_r(value, result);
}

struct tm *localtime(const time_t *value) {
    static struct tm result;
    return gmtime_r(value, &result);
}

time_t mktime(struct tm *value) {
    int64_t year = value->tm_year + 1900 + value->tm_mon / 12;
    int month = value->tm_mon % 12;
    if (month < 0) {
        month += 12;
        year--;
    }
    int64_t y = year - (month < 2);
    int64_t era = floor_div(y, 400);
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (month + (month > 1 ? -2 : 10)) + 2) / 5 + value->tm_mday - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    time_t result = days * 86400 + value->tm_hour * 3600 + value->tm_min * 60 + value->tm_sec;
    gmtime_r(&result, value);
    return result;
}

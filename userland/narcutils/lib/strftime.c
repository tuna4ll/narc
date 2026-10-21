#include <ctype.h>
#include <narcutils/datetime.h>
#include <narcutils/system.h>

static const char *const weekday_names[] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                             "Thursday", "Friday", "Saturday" };
static const char *const month_names[] = { "January", "February", "March", "April",
                                           "May", "June", "July", "August",
                                           "September", "October", "November", "December" };

struct out {
    char *data;
    size_t length, capacity;
};

static void put(struct out *out, char c) {
    if (out->length + 1 >= out->capacity) {
        out->capacity = out->capacity ? out->capacity * 2 : 64;
        out->data = xrealloc(out->data, out->capacity);
    }
    out->data[out->length++] = c;
}

static int64_t floor_div(int64_t a, int64_t b) {
    return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

time_t utc_mktime(struct tm *tm) {
    int64_t year = (int64_t)tm->tm_year + 1900 + floor_div(tm->tm_mon, 12);
    int month = (int)(tm->tm_mon - floor_div(tm->tm_mon, 12) * 12);
    int64_t y = year - (month < 2);
    int64_t era = floor_div(y, 400);
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (month + (month > 1 ? -2 : 10)) + 2) / 5 + tm->tm_mday - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    time_t result = (time_t)(days * 86400 + (int64_t)tm->tm_hour * 3600 + (int64_t)tm->tm_min * 60 +
                             tm->tm_sec);
    gmtime_r(&result, tm);
    return result;
}

long utc_offset(time_t when) {
    struct tm local;
    if (!localtime_r(&when, &local)) return 0;
    return (long)(utc_mktime(&local) - when);
}

bool local_time(time_t when, struct tm *result) {
    return localtime_r(&when, result) != 0;
}

static int iso_week_days(int yday, int wday) {
    int big_enough_multiple_of_7 = (366 / 7 + 2) * 7;
    return yday - (yday - wday + 4 + big_enough_multiple_of_7) % 7 + 3;
}

static bool leap(int64_t year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static void put_padded(struct out *out, const char *text, int width, char pad, int case_mode) {
    size_t length = strlen(text);
    for (int i = (int)length; i < width; i++) put(out, pad == '0' ? '0' : ' ');
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (case_mode > 0) c = (char)toupper((unsigned char)c);
        else if (case_mode < 0) c = (char)tolower((unsigned char)c);
        put(out, c);
    }
}

static void put_number(struct out *out, int64_t value, int digits, char default_pad, char pad,
                       int width) {
    char text[32];
    bool negative = value < 0;
    uint64_t magnitude = negative ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
    int length = snprintf(text, sizeof(text), "%llu", (unsigned long long)magnitude);
    char use_pad = pad ? pad : default_pad;
    int total = width ? width : digits;
    if (use_pad == '-') total = 0;
    if (negative) put(out, '-');
    int count = total - length - negative;
    for (int i = 0; i < count; i++) put(out, use_pad == '_' || use_pad == ' ' ? ' ' : '0');
    for (int i = 0; i < length; i++) put(out, text[i]);
}

static void put_year(struct out *out, int64_t year, char pad, int width) {
    if (pad != '+') {
        put_number(out, year, 4, '0', pad, width);
        return;
    }
    char text[32];
    int length = snprintf(text, sizeof(text), "%lld", (long long)(year < 0 ? -year : year));
    int wanted = width ? width : 4;
    if (year < 0) {
        put_number(out, year, wanted, '0', '0', 0);
        return;
    }
    if (length > 4 || wanted > 4) {
        put(out, '+');
        for (int i = length; i < wanted - 1; i++) put(out, '0');
    } else {
        for (int i = length; i < wanted; i++) put(out, '0');
    }
    for (int i = 0; i < length; i++) put(out, text[i]);
}

static void format_into(struct out *out, const char *format, const struct tm *tm, time_t seconds,
                        long nanoseconds, bool utc) {
    long offset = utc ? 0 : utc_offset(seconds);
    for (const char *f = format; *f; f++) {
        if (*f != '%') {
            put(out, *f);
            continue;
        }
        const char *start = f;
        char pad = 0;
        int case_mode = 0;
        bool swap_case = false;
        for (;;) {
            f++;
            if (*f == '_' || *f == '-' || *f == '0' || *f == '+') pad = *f;
            else if (*f == '^') case_mode = 1;
            else if (*f == '#') swap_case = true;
            else break;
        }
        int width = 0;
        while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
        int colons = 0;
        while (*f == ':') {
            colons++;
            f++;
        }
        if (*f == 'E' || *f == 'O') f++;
        char pad_char = pad == '0' ? '0' : ' ';
        int64_t year = (int64_t)tm->tm_year + 1900;
        char buffer[64];
        switch (*f) {
        case 0:
            f--;
            for (const char *p = start; *p; p++) put(out, *p);
            break;
        case '%':
            put_padded(out, "%", width, pad_char, 0);
            break;
        case 'a':
        case 'A': {
            const char *name = tm->tm_wday >= 0 && tm->tm_wday < 7 ? weekday_names[tm->tm_wday] : "?";
            snprintf(buffer, sizeof(buffer), "%.*s", *f == 'a' ? 3 : 20, name);
            put_padded(out, buffer, width, pad_char, swap_case ? 1 : case_mode);
            break;
        }
        case 'b':
        case 'h':
        case 'B': {
            const char *name = tm->tm_mon >= 0 && tm->tm_mon < 12 ? month_names[tm->tm_mon] : "?";
            snprintf(buffer, sizeof(buffer), "%.*s", *f == 'B' ? 20 : 3, name);
            put_padded(out, buffer, width, pad_char, swap_case ? 1 : case_mode);
            break;
        }
        case 'c': {
            char *sub = format_time("%a %b %e %H:%M:%S %Y", tm, seconds, nanoseconds, utc);
            put_padded(out, sub, width, pad_char, case_mode);
            free(sub);
            break;
        }
        case 'C':
            put_number(out, floor_div(year, 100), 2, '0', pad, width);
            break;
        case 'd':
            put_number(out, tm->tm_mday, 2, '0', pad, width);
            break;
        case 'D':
        case 'F':
        case 'r':
        case 'R':
        case 'T':
        case 'x':
        case 'X': {
            const char *sub_format = *f == 'D' ? "%m/%d/%y" : *f == 'F' ? "%Y-%m-%d"
                                   : *f == 'r' ? "%I:%M:%S %p" : *f == 'R' ? "%H:%M"
                                   : *f == 'T' ? "%H:%M:%S" : *f == 'x' ? "%m/%d/%y" : "%H:%M:%S";
            if (*f == 'F') {
                if (!pad && !width) put_year(out, year, '+', 4);
                else put_year(out, year, pad, width > 6 ? width - 6 : 0);
                char *rest = format_time("-%m-%d", tm, seconds, nanoseconds, utc);
                for (char *p = rest; *p; p++) put(out, *p);
                free(rest);
                break;
            }
            char *sub = format_time(sub_format, tm, seconds, nanoseconds, utc);
            put_padded(out, sub, width, pad_char, case_mode);
            free(sub);
            break;
        }
        case 'e':
            put_number(out, tm->tm_mday, 2, '_', pad, width);
            break;
        case 'g':
        case 'G':
        case 'V': {
            int64_t iso_year = year;
            int days = iso_week_days(tm->tm_yday, tm->tm_wday);
            if (days < 0) {
                iso_year--;
                days = iso_week_days(tm->tm_yday + (leap(iso_year) ? 366 : 365), tm->tm_wday);
            } else {
                int next = iso_week_days(tm->tm_yday - (leap(year) ? 366 : 365), tm->tm_wday);
                if (next >= 0) {
                    iso_year++;
                    days = next;
                }
            }
            if (*f == 'V') put_number(out, days / 7 + 1, 2, '0', pad, width);
            else if (*f == 'G') put_year(out, iso_year, pad, width);
            else put_number(out, ((iso_year % 100) + 100) % 100, 2, '0', pad, width);
            break;
        }
        case 'H':
            put_number(out, tm->tm_hour, 2, '0', pad, width);
            break;
        case 'I':
            put_number(out, (tm->tm_hour + 11) % 12 + 1, 2, '0', pad, width);
            break;
        case 'j':
            put_number(out, tm->tm_yday + 1, 3, '0', pad, width);
            break;
        case 'k':
            put_number(out, tm->tm_hour, 2, '_', pad, width);
            break;
        case 'l':
            put_number(out, (tm->tm_hour + 11) % 12 + 1, 2, '_', pad, width);
            break;
        case 'm':
            put_number(out, tm->tm_mon + 1, 2, '0', pad, width);
            break;
        case 'M':
            put_number(out, tm->tm_min, 2, '0', pad, width);
            break;
        case 'n':
            put_padded(out, "\n", width, pad_char, 0);
            break;
        case 'N': {
            char digits[16];
            snprintf(digits, sizeof(digits), "%09ld", nanoseconds);
            int count = 9;
            if (width && width < count) count = width;
            if (width || pad == '_') {
                while (count > 1 && digits[count - 1] == '0') count--;
            }
            for (int i = 0; i < count; i++) put(out, digits[i]);
            int total = width ? width : 9;
            char fill = pad == '_' ? ' ' : '0';
            if (pad != '-' && (width || pad == '_'))
                for (int i = count; i < total; i++) put(out, fill);
            else if (!width && pad != '_')
                for (int i = count; i < total; i++) put(out, '0');
            break;
        }
        case 'p':
        case 'P': {
            const char *text = tm->tm_hour < 12 ? "AM" : "PM";
            int mode = *f == 'P' || swap_case ? -1 : case_mode;
            put_padded(out, text, width, pad_char, mode);
            break;
        }
        case 'q':
            put_number(out, tm->tm_mon / 3 + 1, 1, '0', pad, width);
            break;
        case 's':
            put_number(out, (int64_t)seconds, 1, '0', pad, width);
            break;
        case 'S':
            put_number(out, tm->tm_sec, 2, '0', pad, width);
            break;
        case 't':
            put_padded(out, "\t", width, pad_char, 0);
            break;
        case 'u':
            put_number(out, (tm->tm_wday + 6) % 7 + 1, 1, '0', pad, width);
            break;
        case 'U':
            put_number(out, (tm->tm_yday - tm->tm_wday + 7) / 7, 2, '0', pad, width);
            break;
        case 'w':
            put_number(out, tm->tm_wday, 1, '0', pad, width);
            break;
        case 'W':
            put_number(out, (tm->tm_yday - (tm->tm_wday - 1 + 7) % 7 + 7) / 7, 2, '0', pad, width);
            break;
        case 'y':
            put_number(out, ((year % 100) + 100) % 100, 2, '0', pad, width);
            break;
        case 'Y':
            put_year(out, year, pad, width);
            break;
        case 'z': {
            long magnitude = offset < 0 ? -offset : offset;
            long hours = magnitude / 3600, minutes = magnitude / 60 % 60, secs = magnitude % 60;
            char sign = offset < 0 ? '-' : '+';
            if (colons == 0) snprintf(buffer, sizeof(buffer), "%c%02ld%02ld", sign, hours, minutes);
            else if (colons == 1) snprintf(buffer, sizeof(buffer), "%c%02ld:%02ld", sign, hours, minutes);
            else if (colons == 2)
                snprintf(buffer, sizeof(buffer), "%c%02ld:%02ld:%02ld", sign, hours, minutes, secs);
            else if (secs)
                snprintf(buffer, sizeof(buffer), "%c%02ld:%02ld:%02ld", sign, hours, minutes, secs);
            else if (minutes)
                snprintf(buffer, sizeof(buffer), "%c%02ld:%02ld", sign, hours, minutes);
            else
                snprintf(buffer, sizeof(buffer), "%c%02ld", sign, hours);
            put_padded(out, buffer, width, pad_char, 0);
            break;
        }
        case 'Z': {
            if (utc || offset == 0) {
                snprintf(buffer, sizeof(buffer), "UTC");
            } else {
                long magnitude = offset < 0 ? -offset : offset;
                if (magnitude % 3600)
                    snprintf(buffer, sizeof(buffer), "%c%02ld%02ld", offset < 0 ? '-' : '+',
                             magnitude / 3600, magnitude / 60 % 60);
                else
                    snprintf(buffer, sizeof(buffer), "%c%02ld", offset < 0 ? '-' : '+', magnitude / 3600);
            }
            put_padded(out, buffer, width, pad_char, swap_case ? -1 : case_mode);
            break;
        }
        default:
            for (const char *p = start; p <= f; p++) put(out, *p);
            break;
        }
    }
}

char *format_time(const char *format, const struct tm *tm, time_t seconds, long nanoseconds,
                  bool utc) {
    struct out out = { 0 };
    format_into(&out, format, tm, seconds, nanoseconds, utc);
    put(&out, 0);
    return out.data;
}

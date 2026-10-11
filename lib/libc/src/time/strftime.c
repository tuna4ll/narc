#include <string.h>
#include <time.h>

static const char *const weekdays[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};
static const char *const months[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};

struct output {
    char *buffer;
    size_t size, length;
};

static void put(struct output *out, const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (out->length + 1 < out->size) out->buffer[out->length] = text[i];
        out->length++;
    }
}

static void put_number(struct output *out, long value, int width, char pad) {
    char digits[24];
    int length = 0;
    int negative = value < 0;
    unsigned long magnitude = negative ? 0UL - (unsigned long)value : (unsigned long)value;
    do {
        digits[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (negative) put(out, "-", 1);
    for (int i = length; i < width; i++) put(out, &pad, 1);
    while (length) put(out, &digits[--length], 1);
}

static void format(struct output *out, const char *spec, const struct tm *tm);

static void conversion(struct output *out, char c, const struct tm *tm) {
    switch (c) {
    case 'a': put(out, weekdays[tm->tm_wday % 7], 3); break;
    case 'A': put(out, weekdays[tm->tm_wday % 7], strlen(weekdays[tm->tm_wday % 7])); break;
    case 'b': case 'h': put(out, months[tm->tm_mon % 12], 3); break;
    case 'B': put(out, months[tm->tm_mon % 12], strlen(months[tm->tm_mon % 12])); break;
    case 'c': format(out, "%a %b %e %H:%M:%S %Y", tm); break;
    case 'C': put_number(out, (tm->tm_year + 1900) / 100, 2, '0'); break;
    case 'd': put_number(out, tm->tm_mday, 2, '0'); break;
    case 'D': format(out, "%m/%d/%y", tm); break;
    case 'e': put_number(out, tm->tm_mday, 2, ' '); break;
    case 'F': format(out, "%Y-%m-%d", tm); break;
    case 'H': put_number(out, tm->tm_hour, 2, '0'); break;
    case 'I': put_number(out, tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, 2, '0'); break;
    case 'j': put_number(out, tm->tm_yday + 1, 3, '0'); break;
    case 'm': put_number(out, tm->tm_mon + 1, 2, '0'); break;
    case 'M': put_number(out, tm->tm_min, 2, '0'); break;
    case 'n': put(out, "\n", 1); break;
    case 'p': put(out, tm->tm_hour < 12 ? "AM" : "PM", 2); break;
    case 'r': format(out, "%I:%M:%S %p", tm); break;
    case 'R': format(out, "%H:%M", tm); break;
    case 'S': put_number(out, tm->tm_sec, 2, '0'); break;
    case 't': put(out, "\t", 1); break;
    case 'T': format(out, "%H:%M:%S", tm); break;
    case 'u': put_number(out, tm->tm_wday ? tm->tm_wday : 7, 1, '0'); break;
    case 'w': put_number(out, tm->tm_wday, 1, '0'); break;
    case 'x': format(out, "%m/%d/%y", tm); break;
    case 'X': format(out, "%H:%M:%S", tm); break;
    case 'y': put_number(out, (tm->tm_year + 1900) % 100, 2, '0'); break;
    case 'Y': put_number(out, tm->tm_year + 1900, 1, '0'); break;
    case 'z': put(out, "+0000", 5); break;
    case 'Z': put(out, "UTC", 3); break;
    case '%': put(out, "%", 1); break;
    default: {
        char text[2] = { '%', c };
        put(out, text, 2);
    }
    }
}

static void format(struct output *out, const char *spec, const struct tm *tm) {
    for (; *spec; spec++) {
        if (*spec != '%' || !spec[1]) {
            put(out, spec, 1);
            continue;
        }
        spec++;
        if (*spec == 'E' || *spec == 'O') spec++;
        if (!*spec) break;
        conversion(out, *spec, tm);
    }
}

size_t strftime(char *buffer, size_t size, const char *spec, const struct tm *value) {
    struct output out = { buffer, size, 0 };
    format(&out, spec, value);
    if (!size || out.length >= size) return 0;
    buffer[out.length] = 0;
    return out.length;
}

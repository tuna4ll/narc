#include <ctype.h>
#include <narcutils/datetime.h>
#include <narcutils/system.h>
#include <strings.h>

enum token_type { T_END, T_UNUMBER, T_SNUMBER, T_WORD, T_PUNCT };
enum meridian { MER_AM, MER_PM, MER_24 };

struct token {
    enum token_type type;
    int64_t value;
    int digits;
    char word[32];
    char punct;
};

struct parser {
    struct token *tokens;
    size_t count, position;
    int64_t year, month, day, hour, minutes, seconds;
    long nanoseconds;
    int year_digits;
    enum meridian meridian;
    int times_seen, dates_seen, days_seen, rels_seen, zones_seen, locals_seen;
    int64_t day_ordinal;
    int day_number;
    int64_t rel_year, rel_month, rel_day, rel_hour, rel_minutes, rel_seconds;
    long rel_ns;
    long zone;
};

static const char *const months[] = { "january", "february", "march", "april", "may", "june",
                                      "july", "august", "september", "october", "november",
                                      "december" };
static const char *const days[] = { "sunday", "monday", "tuesday", "wednesday", "thursday",
                                    "friday", "saturday" };

static const struct {
    const char *name;
    int value;
} ordinals[] = {
    { "last", -1 }, { "this", 0 }, { "next", 1 }, { "first", 1 }, { "third", 3 },
    { "fourth", 4 }, { "fifth", 5 }, { "sixth", 6 }, { "seventh", 7 }, { "eighth", 8 },
    { "ninth", 9 }, { "tenth", 10 }, { "eleventh", 11 }, { "twelfth", 12 },
};

static const struct {
    const char *name;
    int offset;
} zones[] = {
    { "gmt", 0 }, { "ut", 0 }, { "utc", 0 }, { "z", 0 }, { "wet", 0 }, { "west", 60 },
    { "bst", 60 }, { "art", -180 }, { "brt", -180 }, { "brst", -120 }, { "nst", -210 },
    { "ndt", -150 }, { "ast", -240 }, { "adt", -180 }, { "clt", -240 }, { "clst", -180 },
    { "est", -300 }, { "edt", -240 }, { "cst", -360 }, { "cdt", -300 }, { "mst", -420 },
    { "mdt", -360 }, { "pst", -480 }, { "pdt", -420 }, { "akst", -540 }, { "akdt", -480 },
    { "hst", -600 }, { "hast", -600 }, { "hadt", -540 }, { "sst", -660 }, { "wat", 60 },
    { "cet", 60 }, { "cest", 120 }, { "met", 60 }, { "mez", 60 }, { "mesz", 120 },
    { "eet", 120 }, { "eest", 180 }, { "cat", 120 }, { "sast", 120 }, { "eat", 180 },
    { "msk", 180 }, { "msd", 240 }, { "ist", 330 }, { "sgt", 480 }, { "kst", 540 },
    { "jst", 540 }, { "gst", 600 }, { "nzst", 720 }, { "nzdt", 780 }, { "trt", 180 },
};

static bool tokenize(const char *s, struct token **out, size_t *count) {
    size_t capacity = 16, n = 0;
    struct token *tokens = xnmalloc(capacity, sizeof(*tokens));
    for (;;) {
        while (isspace((unsigned char)*s)) s++;
        if (*s == '(') {
            int depth = 0;
            do {
                if (*s == '(') depth++;
                else if (*s == ')') depth--;
                else if (!*s) break;
                s++;
            } while (depth > 0);
            continue;
        }
        if (n + 1 >= capacity) tokens = x2nrealloc(tokens, &capacity, sizeof(*tokens));
        struct token *t = &tokens[n];
        memset(t, 0, sizeof(*t));
        if (!*s) {
            t->type = T_END;
            n++;
            break;
        }
        if (*s == '-' || *s == '+') {
            const char *q = s + 1;
            while (isspace((unsigned char)*q)) q++;
            if (!isdigit((unsigned char)*q)) {
                s++;
                continue;
            }
        }
        if (isdigit((unsigned char)*s) || *s == '-' || *s == '+') {
            bool negative = *s == '-';
            bool sign = *s == '-' || *s == '+';
            if (sign) {
                s++;
                while (isspace((unsigned char)*s)) s++;
            }
            int64_t value = 0;
            int digits = 0;
            while (isdigit((unsigned char)*s)) {
                if (value > (INT64_MAX - 9) / 10) return false;
                value = value * 10 + (*s++ - '0');
                digits++;
            }
            t->type = sign ? T_SNUMBER : T_UNUMBER;
            t->value = negative ? -value : value;
            t->digits = digits;
        } else if (isalpha((unsigned char)*s)) {
            size_t length = 0;
            while (isalpha((unsigned char)*s) || *s == '.') {
                if (*s != '.' && length + 1 < sizeof(t->word))
                    t->word[length++] = (char)tolower((unsigned char)*s);
                s++;
            }
            t->word[length] = 0;
            t->type = T_WORD;
        } else {
            t->type = T_PUNCT;
            t->punct = *s++;
        }
        n++;
    }
    *out = tokens;
    *count = n;
    return true;
}

static struct token *peek(struct parser *p, size_t ahead) {
    size_t index = p->position + ahead;
    return index < p->count ? &p->tokens[index] : &p->tokens[p->count - 1];
}

static bool is_punct(struct token *t, char c) {
    return t->type == T_PUNCT && t->punct == c;
}

static int month_index(const char *word) {
    size_t length = strlen(word);
    if (length < 3) return -1;
    for (int i = 0; i < 12; i++) {
        if (!strcmp(word, months[i])) return i + 1;
        if (length == 3 && !strncmp(word, months[i], 3)) return i + 1;
        if (length == 4 && !strcmp(word, "sept") && i == 8) return 9;
    }
    return -1;
}

static int day_index(const char *word) {
    size_t length = strlen(word);
    for (int i = 0; i < 7; i++) {
        if (!strcmp(word, days[i])) return i;
        if (length == 3 && !strncmp(word, days[i], 3)) return i;
    }
    if (!strcmp(word, "tues")) return 2;
    if (!strcmp(word, "wednes")) return 3;
    if (!strcmp(word, "thur") || !strcmp(word, "thurs")) return 4;
    return -1;
}

static int unit_index(const char *word, int64_t *scale) {
    static const char *const units[] = { "year", "month", "fortnight", "week", "day", "hour",
                                         "minute", "min", "second", "sec" };
    char singular[32];
    snprintf(singular, sizeof(singular), "%s", word);
    size_t length = strlen(singular);
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < ARRAY_SIZE(units); i++) {
            if (strcmp(singular, units[i])) continue;
            *scale = 1;
            switch (i) {
            case 0: return 0;
            case 1: return 1;
            case 2: *scale = 14; return 2;
            case 3: *scale = 7; return 2;
            case 4: return 2;
            case 5: return 3;
            case 6: case 7: return 4;
            default: return 5;
            }
        }
        if (length > 1 && singular[length - 1] == 's') singular[--length] = 0;
        else break;
    }
    return -1;
}

static bool apply_relative(struct parser *p, int unit, int64_t amount) {
    switch (unit) {
    case 0: p->rel_year += amount; break;
    case 1: p->rel_month += amount; break;
    case 2: p->rel_day += amount; break;
    case 3: p->rel_hour += amount; break;
    case 4: p->rel_minutes += amount; break;
    default: p->rel_seconds += amount; break;
    }
    p->rels_seen = 1;
    return true;
}

static bool parse_zone_offset(struct parser *p, int64_t value, int digits, bool has_minutes,
                              int64_t minutes, long *result) {
    int64_t magnitude = value < 0 ? -value : value;
    int64_t hours, mins;
    if (has_minutes) {
        hours = magnitude;
        mins = minutes;
        if (digits > 2 || mins > 59) return false;
    } else if (digits <= 2) {
        hours = magnitude;
        mins = 0;
    } else if (digits <= 4) {
        hours = magnitude / 100;
        mins = magnitude % 100;
    } else {
        return false;
    }
    if (hours > 24 || mins > 59) return false;
    long offset = (long)(hours * 60 + mins);
    *result = value < 0 ? -offset : offset;
    (void)p;
    return true;
}

static bool parse_meridian(struct parser *p) {
    struct token *t = peek(p, 0);
    if (t->type != T_WORD) return false;
    if (!strcmp(t->word, "am")) p->meridian = MER_AM;
    else if (!strcmp(t->word, "pm")) p->meridian = MER_PM;
    else return false;
    p->position++;
    return true;
}

static bool parse_time_tail(struct parser *p) {
    struct token *t = peek(p, 0);
    if (t->type == T_SNUMBER) {
        int64_t value = t->value;
        int digits = t->digits;
        p->position++;
        bool has_minutes = false;
        int64_t minutes = 0;
        if (is_punct(peek(p, 0), ':') && peek(p, 1)->type == T_UNUMBER) {
            has_minutes = true;
            minutes = peek(p, 1)->value;
            p->position += 2;
        }
        long offset;
        if (!parse_zone_offset(p, value, digits, has_minutes, minutes, &offset)) return false;
        p->zone = offset;
        p->zones_seen++;
    } else {
        parse_meridian(p);
    }
    return true;
}

static bool parse_time(struct parser *p) {
    p->hour = peek(p, 0)->value;
    p->minutes = peek(p, 2)->value;
    p->position += 3;
    p->seconds = 0;
    p->nanoseconds = 0;
    if (is_punct(peek(p, 0), ':') && peek(p, 1)->type == T_UNUMBER) {
        p->seconds = peek(p, 1)->value;
        p->position += 2;
        if ((is_punct(peek(p, 0), '.') || is_punct(peek(p, 0), ',')) && peek(p, 1)->type == T_UNUMBER) {
            struct token *fraction = peek(p, 1);
            int64_t value = fraction->value;
            int digits = fraction->digits;
            while (digits > 9) {
                value /= 10;
                digits--;
            }
            while (digits < 9) {
                value *= 10;
                digits++;
            }
            p->nanoseconds = (long)value;
            p->position += 2;
        }
    }
    p->meridian = MER_24;
    p->times_seen++;
    return parse_time_tail(p);
}

static void set_year(struct parser *p, int64_t value, int digits) {
    p->year = value;
    p->year_digits = digits;
}

static bool parse_item(struct parser *p) {
    struct token *t = peek(p, 0);
    struct token *n1 = peek(p, 1);
    struct token *n2 = peek(p, 2);

    if (t->type == T_UNUMBER && is_punct(n1, ':') && n2->type == T_UNUMBER) return parse_time(p);

    if (t->type == T_UNUMBER && n1->type == T_SNUMBER && n1->value <= 0 && n2->type == T_SNUMBER &&
        n2->value <= 0) {
        set_year(p, t->value, t->digits);
        p->month = -n1->value;
        p->day = -n2->value;
        p->dates_seen++;
        p->position += 3;
        if (peek(p, 0)->type == T_WORD && !strcmp(peek(p, 0)->word, "t")) {
            p->position++;
            if (peek(p, 0)->type == T_UNUMBER && is_punct(peek(p, 1), ':') &&
                peek(p, 2)->type == T_UNUMBER)
                return parse_time(p);
            return false;
        }
        return true;
    }

    if (t->type == T_UNUMBER && is_punct(n1, '/') && n2->type == T_UNUMBER) {
        p->position += 3;
        if (is_punct(peek(p, 0), '/') && peek(p, 1)->type == T_UNUMBER) {
            struct token *third = peek(p, 1);
            if (t->digits >= 4) {
                set_year(p, t->value, t->digits);
                p->month = n2->value;
                p->day = third->value;
            } else {
                p->month = t->value;
                p->day = n2->value;
                set_year(p, third->value, third->digits);
            }
            p->position += 2;
        } else {
            p->month = t->value;
            p->day = n2->value;
        }
        p->dates_seen++;
        return true;
    }

    if (t->type == T_WORD) {
        if (t->word[0] == 0) {
            p->position++;
            return true;
        }
        int month = month_index(t->word);
        if (month > 0) {
            p->position++;
            p->month = month;
            p->dates_seen++;
            if (is_punct(peek(p, 0), '-') || is_punct(peek(p, 0), '.')) p->position++;
            struct token *d = peek(p, 0);
            if (d->type == T_UNUMBER && !is_punct(peek(p, 1), ':')) {
                p->day = d->value;
                p->position++;
                if (is_punct(peek(p, 0), ',')) p->position++;
                struct token *y = peek(p, 0);
                if (y->type == T_UNUMBER && !is_punct(peek(p, 1), ':')) {
                    set_year(p, y->value, y->digits);
                    p->position++;
                } else if (y->type == T_SNUMBER && y->value < 0) {
                    set_year(p, -y->value, y->digits);
                    p->position++;
                }
            } else if (d->type == T_SNUMBER && d->value < 0 && peek(p, 1)->type == T_SNUMBER &&
                       peek(p, 1)->value < 0) {
                p->day = -d->value;
                set_year(p, -peek(p, 1)->value, peek(p, 1)->digits);
                p->position += 2;
            } else {
                return false;
            }
            return true;
        }
        int day = day_index(t->word);
        if (day >= 0) {
            p->position++;
            if (is_punct(peek(p, 0), ',')) p->position++;
            p->day_number = day;
            if (!p->days_seen) p->day_ordinal = 0;
            p->days_seen++;
            return true;
        }
        for (size_t i = 0; i < ARRAY_SIZE(ordinals); i++) {
            if (strcmp(t->word, ordinals[i].name)) continue;
            struct token *next = peek(p, 1);
            if (next->type != T_WORD) return false;
            int64_t scale;
            int unit = unit_index(next->word, &scale);
            if (unit >= 0) {
                p->position += 2;
                int64_t amount = ordinals[i].value * scale;
                if (peek(p, 0)->type == T_WORD && !strcmp(peek(p, 0)->word, "ago")) {
                    amount = -amount;
                    p->position++;
                }
                return apply_relative(p, unit, amount);
            }
            int wday = day_index(next->word);
            if (wday >= 0) {
                p->position += 2;
                if (is_punct(peek(p, 0), ',')) p->position++;
                p->day_ordinal = ordinals[i].value;
                p->day_number = wday;
                p->days_seen++;
                return true;
            }
            return false;
        }
        int64_t scale;
        int unit = unit_index(t->word, &scale);
        if (unit >= 0) {
            p->position++;
            int64_t amount = scale;
            if (peek(p, 0)->type == T_WORD && !strcmp(peek(p, 0)->word, "ago")) {
                amount = -amount;
                p->position++;
            }
            return apply_relative(p, unit, amount);
        }
        if (!strcmp(t->word, "tomorrow") || !strcmp(t->word, "yesterday") ||
            !strcmp(t->word, "today") || !strcmp(t->word, "now")) {
            p->position++;
            return apply_relative(p, 2, t->word[0] == 't' && t->word[2] == 'm' ? 1
                                        : t->word[0] == 'y' ? -1 : 0);
        }
        if (!strcmp(t->word, "t") && peek(p, 1)->type == T_UNUMBER) {
            p->position++;
            return true;
        }
        for (size_t i = 0; i < ARRAY_SIZE(zones); i++) {
            if (strcmp(t->word, zones[i].name)) continue;
            p->position++;
            p->zone = zones[i].offset;
            p->zones_seen++;
            struct token *offset = peek(p, 0);
            int64_t unused_scale;
            bool relative = peek(p, 1)->type == T_WORD && unit_index(peek(p, 1)->word, &unused_scale) >= 0;
            if (offset->type == T_SNUMBER && !relative) {
                p->position++;
                bool has_minutes = false;
                int64_t minutes = 0;
                if (is_punct(peek(p, 0), ':') && peek(p, 1)->type == T_UNUMBER) {
                    has_minutes = true;
                    minutes = peek(p, 1)->value;
                    p->position += 2;
                }
                long extra;
                if (!parse_zone_offset(p, offset->value, offset->digits, has_minutes, minutes, &extra))
                    return false;
                p->zone += extra;
            }
            if (peek(p, 0)->type == T_WORD && !strcmp(peek(p, 0)->word, "dst")) {
                p->zone += 60;
                p->position++;
            }
            return true;
        }
        if (t->word[0] && !t->word[1] && t->word[0] != 'j') {
            char letter = t->word[0];
            int hours = letter <= 'i' ? letter - 'a' + 1 : letter <= 'm' ? letter - 'a'
                      : letter <= 'y' ? -(letter - 'n' + 1) : 0;
            p->position++;
            p->zone = hours * 60;
            p->zones_seen++;
            return true;
        }
        if (!strcmp(t->word, "j")) {
            p->position++;
            return true;
        }
        return false;
    }

    if ((t->type == T_UNUMBER || t->type == T_SNUMBER) && n1->type == T_WORD) {
        int month = month_index(n1->word);
        if (month > 0 && t->type == T_UNUMBER) {
            p->day = t->value;
            p->month = month;
            p->dates_seen++;
            p->position += 2;
            struct token *y = peek(p, 0);
            if (y->type == T_SNUMBER && y->value < 0) {
                set_year(p, -y->value, y->digits);
                p->position++;
            } else if (y->type == T_UNUMBER && !is_punct(peek(p, 1), ':')) {
                set_year(p, y->value, y->digits);
                p->position++;
            }
            return true;
        }
        int64_t scale;
        int unit = unit_index(n1->word, &scale);
        if (unit >= 0) {
            p->position += 2;
            int64_t amount = t->value * scale;
            if (peek(p, 0)->type == T_WORD && !strcmp(peek(p, 0)->word, "ago")) {
                amount = -amount;
                p->position++;
            }
            return apply_relative(p, unit, amount);
        }
        int wday = day_index(n1->word);
        if (wday >= 0 && t->type == T_UNUMBER) {
            p->position += 2;
            p->day_ordinal = t->value;
            p->day_number = wday;
            p->days_seen++;
            return true;
        }
        if (t->type == T_UNUMBER && (!strcmp(n1->word, "am") || !strcmp(n1->word, "pm"))) {
            p->hour = t->value;
            p->minutes = 0;
            p->seconds = 0;
            p->nanoseconds = 0;
            p->times_seen++;
            p->position++;
            parse_meridian(p);
            return parse_time_tail(p);
        }
    }

    if (t->type == T_UNUMBER && n1->type == T_PUNCT && n1->punct == '.' && n2->type == T_UNUMBER &&
        is_punct(peek(p, 3), '.') && peek(p, 4)->type == T_UNUMBER) {
        p->day = t->value;
        p->month = n2->value;
        set_year(p, peek(p, 4)->value, peek(p, 4)->digits);
        p->dates_seen++;
        p->position += 5;
        return true;
    }

    if (t->type == T_UNUMBER) {
        p->position++;
        if (p->dates_seen && !p->year_digits && !p->rels_seen && (p->times_seen || t->digits > 2)) {
            set_year(p, t->value, t->digits);
        } else if (t->digits > 4) {
            p->dates_seen++;
            p->day = t->value % 100;
            p->month = (t->value / 100) % 100;
            set_year(p, t->value / 10000, t->digits - 4);
        } else {
            p->times_seen++;
            if (t->digits <= 2) {
                p->hour = t->value;
                p->minutes = 0;
            } else {
                p->hour = t->value / 100;
                p->minutes = t->value % 100;
            }
            p->seconds = 0;
            p->nanoseconds = 0;
            p->meridian = MER_24;
            if (peek(p, 0)->type == T_WORD) parse_meridian(p);
        }
        return true;
    }

    if (t->type == T_SNUMBER && p->times_seen && !p->zones_seen) {
        return parse_time_tail(p);
    }
    if (is_punct(t, ',')) {
        p->position++;
        return true;
    }
    return false;
}

static int64_t to_year(int64_t year, int digits) {
    if (digits == 2) return year + (year < 69 ? 2000 : 1900);
    return year;
}

static int64_t to_hour(int64_t hours, enum meridian meridian) {
    switch (meridian) {
    case MER_24: return 0 <= hours && hours < 24 ? hours : -1;
    case MER_AM: return 0 < hours && hours < 12 ? hours : hours == 12 ? 0 : -1;
    default: return 0 < hours && hours < 12 ? hours + 12 : hours == 12 ? 12 : -1;
    }
}

bool parse_datetime(struct timespec *result, const char *string, const struct timespec *now) {
    while (isspace((unsigned char)*string)) string++;
    if (!strncasecmp(string, "TZ=\"", 4)) {
        const char *close = strchr(string + 4, '"');
        if (!close) return false;
        string = close + 1;
    }
    if (*string == '@') {
        const char *s = string + 1;
        char *end;
        errno = 0;
        long long seconds = strtoll(s, &end, 10);
        if (end == s || errno) return false;
        long ns = 0;
        if (*end == '.' || *end == ',') {
            end++;
            int digits = 0;
            while (isdigit((unsigned char)*end)) {
                if (digits < 9) ns = ns * 10 + (*end - '0');
                digits++;
                end++;
            }
            while (digits++ < 9) ns *= 10;
            if (seconds < 0 || (s[0] == '-' && seconds == 0)) {
                if (ns) {
                    seconds--;
                    ns = 1000000000L - ns;
                }
            }
        }
        while (isspace((unsigned char)*end)) end++;
        if (*end) return false;
        result->tv_sec = (time_t)seconds;
        result->tv_nsec = ns;
        return true;
    }

    struct parser p;
    memset(&p, 0, sizeof(p));
    if (!tokenize(string, &p.tokens, &p.count)) return false;
    struct tm tm;
    if (!local_time(now->tv_sec, &tm)) {
        free(p.tokens);
        return false;
    }
    p.year = tm.tm_year + 1900;
    p.month = tm.tm_mon + 1;
    p.day = tm.tm_mday;
    p.hour = tm.tm_hour;
    p.minutes = tm.tm_min;
    p.seconds = tm.tm_sec;
    p.nanoseconds = now->tv_nsec;
    p.meridian = MER_24;

    while (peek(&p, 0)->type != T_END) {
        if (!parse_item(&p)) {
            free(p.tokens);
            return false;
        }
    }
    free(p.tokens);
    if (p.times_seen > 1 || p.dates_seen > 1 || p.days_seen > 1 || p.zones_seen > 1) return false;

    struct tm target;
    memset(&target, 0, sizeof(target));
    target.tm_year = (int)(to_year(p.year, p.year_digits) - 1900);
    target.tm_mon = (int)(p.month - 1);
    target.tm_mday = (int)p.day;
    long ns = 0;
    if (p.times_seen || (p.rels_seen && !p.dates_seen && !p.days_seen)) {
        int64_t hour = to_hour(p.hour, p.meridian);
        if (hour < 0 || p.minutes < 0 || p.minutes > 59 || p.seconds < 0 || p.seconds > 59) return false;
        target.tm_hour = (int)hour;
        target.tm_min = (int)p.minutes;
        target.tm_sec = (int)p.seconds;
        ns = p.nanoseconds;
    }
    if (p.month < 1 || p.month > 12 || p.day < 1 || p.day > 31) return false;
    struct tm check = target;
    time_t base = p.zones_seen ? utc_mktime(&check) - p.zone * 60 : utc_mktime(&check);
    if (check.tm_mday != target.tm_mday || check.tm_mon != target.tm_mon) return false;
    if (!p.zones_seen) {
        struct tm local = target;
        local.tm_isdst = -1;
        base = mktime(&local);
    }

    if (p.days_seen && !p.dates_seen) {
        struct tm day_tm;
        if (p.zones_seen) {
            time_t shifted = base + p.zone * 60;
            gmtime_r(&shifted, &day_tm);
        } else {
            local_time(base, &day_tm);
        }
        int wday = day_tm.tm_wday;
        int64_t adjust = ((p.day_number - wday + 7) % 7) +
                         7 * (p.day_ordinal - (0 < p.day_ordinal && wday != p.day_number));
        target.tm_mday += (int)adjust;
        struct tm again = target;
        again.tm_isdst = -1;
        base = p.zones_seen ? utc_mktime(&again) - p.zone * 60 : mktime(&again);
    }

    if (p.rel_year || p.rel_month || p.rel_day) {
        struct tm rel;
        if (p.zones_seen) {
            time_t shifted = base + p.zone * 60;
            gmtime_r(&shifted, &rel);
        } else {
            local_time(base, &rel);
        }
        int64_t year = rel.tm_year + p.rel_year;
        int64_t month = rel.tm_mon + p.rel_month;
        int64_t day = rel.tm_mday + p.rel_day;
        if (year < INT_MIN || year > INT_MAX || month < INT_MIN || month > INT_MAX || day < INT_MIN ||
            day > INT_MAX)
            return false;
        rel.tm_year = (int)year;
        rel.tm_mon = (int)month;
        rel.tm_mday = (int)day;
        rel.tm_isdst = -1;
        base = p.zones_seen ? utc_mktime(&rel) - p.zone * 60 : mktime(&rel);
    }

    int64_t delta = p.rel_hour * 3600 + p.rel_minutes * 60 + p.rel_seconds;
    int64_t seconds = (int64_t)base + delta;
    int64_t total_ns = ns + p.rel_ns;
    seconds += total_ns / 1000000000L;
    total_ns %= 1000000000L;
    if (total_ns < 0) {
        total_ns += 1000000000L;
        seconds--;
    }
    result->tv_sec = (time_t)seconds;
    result->tv_nsec = (long)total_ns;
    return true;
}

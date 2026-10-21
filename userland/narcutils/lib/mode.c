#include <narcutils/mode.h>
#include <narcutils/system.h>

enum { MODE_DONE, MODE_ORDINARY_CHANGE, MODE_X_IF_ANY_X, MODE_COPY_EXISTING };

struct mode_change {
    char op;
    char flag;
    mode_t affected;
    mode_t value;
    mode_t mentioned;
};

static struct mode_change *make_node_op_equals(mode_t new_mode, mode_t mentioned) {
    struct mode_change *p = xnmalloc(2, sizeof(*p));
    p->op = '=';
    p->flag = MODE_ORDINARY_CHANGE;
    p->affected = CHMOD_MODE_BITS;
    p->value = new_mode;
    p->mentioned = mentioned;
    p[1].flag = MODE_DONE;
    return p;
}

struct mode_change *mode_compile(const char *mode_string) {
    const char *p;
    if ('0' <= *mode_string && *mode_string < '8') {
        unsigned octal_mode = 0;
        p = mode_string;
        do {
            octal_mode = 8 * octal_mode + (unsigned)(*p++ - '0');
            if (octal_mode > CHMOD_MODE_BITS) return 0;
        } while ('0' <= *p && *p < '8');
        if (*p) return 0;
        mode_t mode = octal_mode;
        mode_t mentioned = p - mode_string < 5 ? (mode & (S_ISUID | S_ISGID)) | S_ISVTX | S_IRWXUGO
                                              : CHMOD_MODE_BITS;
        return make_node_op_equals(mode, mentioned);
    }

    size_t needed = 1;
    for (p = mode_string; *p; p++) needed += *p == '=' || *p == '+' || *p == '-';
    struct mode_change *mc = xnmalloc(needed, sizeof(*mc));
    size_t used = 0;

    for (p = mode_string;; p++) {
        mode_t affected = 0;
        for (;; p++) {
            switch (*p) {
            case 'u': affected |= S_ISUID | S_IRWXU; continue;
            case 'g': affected |= S_ISGID | S_IRWXG; continue;
            case 'o': affected |= S_ISVTX | S_IRWXO; continue;
            case 'a': affected |= CHMOD_MODE_BITS; continue;
            case '=': case '+': case '-': break;
            default: goto invalid;
            }
            break;
        }
        do {
            char op = *p++;
            mode_t value;
            mode_t mentioned = 0;
            char flag = MODE_COPY_EXISTING;
            switch (*p) {
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                unsigned octal_mode = 0;
                do {
                    octal_mode = 8 * octal_mode + (unsigned)(*p++ - '0');
                    if (octal_mode > CHMOD_MODE_BITS) goto invalid;
                } while ('0' <= *p && *p < '8');
                if (affected || (*p && *p != ',')) goto invalid;
                affected = mentioned = CHMOD_MODE_BITS;
                value = octal_mode;
                flag = MODE_ORDINARY_CHANGE;
                break;
            }
            case 'u': value = S_IRWXU; p++; break;
            case 'g': value = S_IRWXG; p++; break;
            case 'o': value = S_IRWXO; p++; break;
            default:
                value = 0;
                flag = MODE_ORDINARY_CHANGE;
                for (;; p++) {
                    switch (*p) {
                    case 'r': value |= S_IRUSR | S_IRGRP | S_IROTH; continue;
                    case 'w': value |= S_IWUSR | S_IWGRP | S_IWOTH; continue;
                    case 'x': value |= S_IXUSR | S_IXGRP | S_IXOTH; continue;
                    case 'X': flag = MODE_X_IF_ANY_X; continue;
                    case 's': value |= S_ISUID | S_ISGID; continue;
                    case 't': value |= S_ISVTX; continue;
                    default: break;
                    }
                    break;
                }
            }
            struct mode_change *change = &mc[used++];
            change->op = op;
            change->flag = flag;
            change->affected = affected;
            change->value = value;
            change->mentioned = mentioned ? mentioned : affected ? affected & value : value;
        } while (*p == '=' || *p == '+' || *p == '-');
        if (*p != ',') break;
    }
    if (*p == 0) {
        mc[used].flag = MODE_DONE;
        return mc;
    }
invalid:
    free(mc);
    return 0;
}

struct mode_change *mode_create_from_ref(const char *reference_file) {
    struct stat st;
    if (stat(reference_file, &st) != 0) return 0;
    return make_node_op_equals(st.st_mode, CHMOD_MODE_BITS);
}

mode_t mode_adjust(mode_t old_mode, bool directory, mode_t umask_value,
                   const struct mode_change *changes, mode_t *mode_bits_out) {
    mode_t new_mode = old_mode & CHMOD_MODE_BITS;
    mode_t mode_bits = 0;
    for (; changes->flag != MODE_DONE; changes++) {
        mode_t affected = changes->affected;
        mode_t omit_change = (directory ? S_ISUID | S_ISGID : 0) & ~changes->mentioned;
        mode_t value = changes->value;
        switch (changes->flag) {
        case MODE_ORDINARY_CHANGE:
            break;
        case MODE_COPY_EXISTING:
            value &= new_mode;
            value |= ((value & (S_IRUSR | S_IRGRP | S_IROTH) ? S_IRUSR | S_IRGRP | S_IROTH : 0) |
                      (value & (S_IWUSR | S_IWGRP | S_IWOTH) ? S_IWUSR | S_IWGRP | S_IWOTH : 0) |
                      (value & (S_IXUSR | S_IXGRP | S_IXOTH) ? S_IXUSR | S_IXGRP | S_IXOTH : 0));
            break;
        case MODE_X_IF_ANY_X:
            if ((new_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) || directory)
                value |= S_IXUSR | S_IXGRP | S_IXOTH;
            break;
        }
        value &= (affected ? affected : ~umask_value) & ~omit_change;
        switch (changes->op) {
        case '=': {
            mode_t preserved = (affected ? ~affected : 0) | omit_change;
            mode_bits |= CHMOD_MODE_BITS & ~preserved;
            new_mode = (new_mode & preserved) | value;
            break;
        }
        case '+':
            mode_bits |= value;
            new_mode |= value;
            break;
        case '-':
            mode_bits |= value;
            new_mode &= ~value;
            break;
        }
    }
    if (mode_bits_out) *mode_bits_out = mode_bits;
    return new_mode;
}

static char ftypelet(mode_t bits) {
    if (S_ISREG(bits)) return '-';
    if (S_ISDIR(bits)) return 'd';
    if (S_ISBLK(bits)) return 'b';
    if (S_ISCHR(bits)) return 'c';
    if (S_ISLNK(bits)) return 'l';
    if (S_ISFIFO(bits)) return 'p';
    if (S_ISSOCK(bits)) return 's';
    return '?';
}

void strmode(mode_t mode, char *str) {
    str[0] = ftypelet(mode);
    str[1] = mode & S_IRUSR ? 'r' : '-';
    str[2] = mode & S_IWUSR ? 'w' : '-';
    str[3] = mode & S_ISUID ? (mode & S_IXUSR ? 's' : 'S') : (mode & S_IXUSR ? 'x' : '-');
    str[4] = mode & S_IRGRP ? 'r' : '-';
    str[5] = mode & S_IWGRP ? 'w' : '-';
    str[6] = mode & S_ISGID ? (mode & S_IXGRP ? 's' : 'S') : (mode & S_IXGRP ? 'x' : '-');
    str[7] = mode & S_IROTH ? 'r' : '-';
    str[8] = mode & S_IWOTH ? 'w' : '-';
    str[9] = mode & S_ISVTX ? (mode & S_IXOTH ? 't' : 'T') : (mode & S_IXOTH ? 'x' : '-');
    str[10] = ' ';
    str[11] = 0;
}

void filemodestring(const struct stat *st, char *string) {
    strmode(st->st_mode, string);
}

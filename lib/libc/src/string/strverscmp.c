#include <ctype.h>
#include <string.h>

int strverscmp(const char *left, const char *right) {
    const unsigned char *l = (const unsigned char *)left, *r = (const unsigned char *)right;
    size_t i = 0, start = 0;
    while (l[i] == r[i]) {
        if (!l[i]) return 0;
        if (!isdigit(l[i])) start = i + 1;
        i++;
    }
    int ld = isdigit(l[i]) != 0, rd = isdigit(r[i]) != 0;
    if (start == i && !(ld && rd)) return l[i] - r[i];
    if (l[start] == '0' || r[start] == '0') {
        if (ld != rd) return ld ? -1 : 1;
        return l[i] - r[i];
    }
    size_t j = i;
    while (isdigit(l[j]) && isdigit(r[j])) j++;
    if (isdigit(l[j])) return 1;
    if (isdigit(r[j])) return -1;
    return l[i] - r[i];
}

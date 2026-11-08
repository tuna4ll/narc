#ifndef KLIBC_KPRINT_H
#define KLIBC_KPRINT_H

__attribute__((format(printf, 1, 2)))
void kprint(const char *fmt, ...);

#endif /* KLIBC_KPRINT_H */

#ifndef KLIBC_KPRINT_H
#define KLIBC_KPRINT_H

__attribute__((format(printf, 1, 2)))
void kprint(const char *fmt, ...);

__attribute__((noreturn, format(printf, 1, 2)))
void panic(const char *fmt, ...);

#endif /* KLIBC_KPRINT_H */

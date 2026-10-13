#pragma once
#include <kernel/file.h>

#define TTY_VINTR    0
#define TTY_VQUIT    1
#define TTY_VERASE   2
#define TTY_VKILL    3
#define TTY_VEOF     4
#define TTY_VTIME    5
#define TTY_VMIN     6
#define TTY_VSTART   8
#define TTY_VSTOP    9
#define TTY_VSUSP    10
#define TTY_VEOL     11
#define TTY_VREPRINT 12
#define TTY_VDISCARD 13
#define TTY_VWERASE  14
#define TTY_VLNEXT   15
#define TTY_VEOL2    16

#define TTY_IGNCR   0x0080u
#define TTY_ICRNL   0x0100u
#define TTY_INLCR   0x0040u
#define TTY_ISTRIP  0x0020u
#define TTY_IUTF8   0x4000u

#define TTY_OPOST   0x0001u
#define TTY_ONLCR   0x0004u
#define TTY_OCRNL   0x0008u

#define TTY_ISIG    0x0001u
#define TTY_ICANON  0x0002u
#define TTY_ECHO    0x0008u
#define TTY_ECHOE   0x0010u
#define TTY_ECHOK   0x0020u
#define TTY_ECHONL  0x0040u
#define TTY_NOFLSH  0x0080u
#define TTY_TOSTOP  0x0100u
#define TTY_ECHOCTL 0x0200u
#define TTY_ECHOKE  0x0800u
#define TTY_IEXTEN  0x8000u

struct tty;

void tty_init(void);
void tty_input(char c);
struct open_file *tty_open(void);
struct tty *tty_console(void);
void tty_session_exit(struct tty *tty, int session);

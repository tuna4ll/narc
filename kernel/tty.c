#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/mutex.h>
#include <kernel/serial.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/tty.h>
#include <kernel/uaccess.h>
#include <kernel/wait.h>

#define TTY_BUFFER 4096
#define TTY_LINE 1024
#define CTRL(c) ((c) & 0x1f)
#define LINE_END 1
#define LINE_EOF 2

struct tty {
    struct spinlock lock;
    narc_termios_t termios;
    narc_winsize_t winsize;
    int pgrp, session;
    char queue[TTY_BUFFER];
    uint8_t marks[TTY_BUFFER];
    size_t head, count, lines;
    char edit[TTY_LINE];
    size_t edit_count;
    int literal;
    struct wait_queue readable;
    struct mutex read_lock, write_lock;
};

static struct tty console = {
    .lock = SPINLOCK_INIT,
    .readable = WAIT_QUEUE_INIT,
    .read_lock = MUTEX_INIT,
    .write_lock = MUTEX_INIT,
};

static const narc_termios_t default_termios = {
    .iflag = TTY_ICRNL | TTY_IUTF8,
    .oflag = TTY_OPOST | TTY_ONLCR,
    .cflag = 0x00bf,
    .lflag = TTY_ISIG | TTY_ICANON | TTY_ECHO | TTY_ECHOE | TTY_ECHOK | TTY_ECHOCTL |
             TTY_ECHOKE | TTY_IEXTEN,
    .cc = {
        [TTY_VINTR] = CTRL('C'), [TTY_VQUIT] = 0x1c, [TTY_VERASE] = 0x7f,
        [TTY_VKILL] = CTRL('U'), [TTY_VEOF] = CTRL('D'), [TTY_VTIME] = 0,
        [TTY_VMIN] = 1, [TTY_VSTART] = CTRL('Q'), [TTY_VSTOP] = CTRL('S'),
        [TTY_VSUSP] = CTRL('Z'), [TTY_VREPRINT] = CTRL('R'), [TTY_VDISCARD] = CTRL('O'),
        [TTY_VWERASE] = CTRL('W'), [TTY_VLNEXT] = CTRL('V'),
    },
    .ispeed = 38400,
    .ospeed = 38400,
};

struct tty *tty_console(void) {
    return &console;
}

void tty_init(void) {
    console.termios = default_termios;
    console.winsize.rows = console_rows();
    console.winsize.cols = console_cols();
    serial_rx_enable();
    arch_serial_irq_init();
}

static void output_raw(const char *data, size_t length) {
    console_write(data, length);
}

static void output(struct tty *tty, const char *data, size_t length) {
    if (!(tty->termios.oflag & TTY_OPOST)) {
        output_raw(data, length);
        return;
    }
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        char c = data[i];
        if (c == '\n' && (tty->termios.oflag & TTY_ONLCR)) {
            output_raw(data + start, i - start);
            output_raw("\r\n", 2);
            start = i + 1;
        } else if (c == '\r' && (tty->termios.oflag & TTY_OCRNL)) {
            output_raw(data + start, i - start);
            output_raw("\n", 1);
            start = i + 1;
        }
    }
    output_raw(data + start, length - start);
}

static int control(char c) {
    return ((unsigned char)c < 32 && c != '\t' && c != '\n') || c == 0x7f;
}

static void echo_char(struct tty *tty, char c) {
    if (control(c) && (tty->termios.lflag & TTY_ECHOCTL)) {
        char text[2] = { '^', c == 0x7f ? '?' : (char)(c + '@') };
        output(tty, text, 2);
    } else {
        output(tty, &c, 1);
    }
}

static size_t echo_width(struct tty *tty, char c) {
    return control(c) && (tty->termios.lflag & TTY_ECHOCTL) ? 2 : 1;
}

static void erase_one(struct tty *tty) {
    if (!tty->edit_count) return;
    char c = tty->edit[--tty->edit_count];
    if (!(tty->termios.lflag & TTY_ECHO)) return;
    if (!(tty->termios.lflag & TTY_ECHOE)) {
        echo_char(tty, tty->termios.cc[TTY_VERASE]);
        return;
    }
    for (size_t i = echo_width(tty, c); i; i--) output(tty, "\b \b", 3);
}

static void push(struct tty *tty, char c, uint8_t mark) {
    if (tty->count == TTY_BUFFER) return;
    size_t slot = (tty->head + tty->count++) % TTY_BUFFER;
    tty->queue[slot] = c;
    tty->marks[slot] = mark;
    if (mark) tty->lines++;
}

static void commit_line(struct tty *tty, uint8_t mark) {
    if (TTY_BUFFER - tty->count < tty->edit_count + 1) {
        tty->edit_count = 0;
        return;
    }
    for (size_t i = 0; i < tty->edit_count; i++)
        push(tty, tty->edit[i], i + 1 == tty->edit_count && mark == LINE_END ? LINE_END : 0);
    if (mark == LINE_EOF) push(tty, 0, LINE_EOF);
    tty->edit_count = 0;
}

static void flush_input(struct tty *tty) {
    tty->head = tty->count = tty->lines = 0;
    tty->edit_count = 0;
}

static int interrupt_char(struct tty *tty, char c) {
    const uint8_t *cc = tty->termios.cc;
    int signal = 0;
    if (c == cc[TTY_VINTR]) signal = NARC_SIGINT;
    else if (c == cc[TTY_VQUIT]) signal = NARC_SIGQUIT;
    else if (c == cc[TTY_VSUSP]) signal = NARC_SIGTSTP;
    if (!signal || !c) return 0;
    if (!(tty->termios.lflag & TTY_NOFLSH)) flush_input(tty);
    if (tty->termios.lflag & TTY_ECHO) echo_char(tty, c);
    if (tty->pgrp > 0) task_signal_group(tty->pgrp, signal);
    return 1;
}

static void canonical_char(struct tty *tty, char c) {
    const uint8_t *cc = tty->termios.cc;
    uint32_t lflag = tty->termios.lflag;
    int extended = (lflag & TTY_IEXTEN) != 0;
    if (tty->literal) {
        tty->literal = 0;
    } else if (extended && c == cc[TTY_VLNEXT] && c) {
        tty->literal = 1;
        if (lflag & TTY_ECHO) output(tty, "^\b", 2);
        return;
    } else if (c == cc[TTY_VERASE] && c) {
        erase_one(tty);
        return;
    } else if (extended && c == cc[TTY_VWERASE] && c) {
        while (tty->edit_count && tty->edit[tty->edit_count - 1] == ' ') erase_one(tty);
        while (tty->edit_count && tty->edit[tty->edit_count - 1] != ' ') erase_one(tty);
        return;
    } else if (c == cc[TTY_VKILL] && c) {
        if ((lflag & TTY_ECHO) && !(lflag & TTY_ECHOKE)) {
            echo_char(tty, c);
            if (lflag & TTY_ECHOK) output(tty, "\n", 1);
            tty->edit_count = 0;
            return;
        }
        while (tty->edit_count) erase_one(tty);
        return;
    } else if (extended && c == cc[TTY_VREPRINT] && c) {
        if (lflag & TTY_ECHO) {
            echo_char(tty, c);
            output(tty, "\n", 1);
            for (size_t i = 0; i < tty->edit_count; i++) echo_char(tty, tty->edit[i]);
        }
        return;
    } else if (c == cc[TTY_VEOF] && c) {
        commit_line(tty, tty->edit_count ? LINE_END : LINE_EOF);
        wake_up_all(&tty->readable);
        return;
    }
    if (c == '\n' || (c && (c == cc[TTY_VEOL] || c == cc[TTY_VEOL2]))) {
        if (tty->edit_count < TTY_LINE) tty->edit[tty->edit_count++] = c;
        if ((lflag & TTY_ECHO) || (c == '\n' && (lflag & TTY_ECHONL))) output(tty, &c, 1);
        commit_line(tty, LINE_END);
        wake_up_all(&tty->readable);
        return;
    }
    if (tty->edit_count >= TTY_LINE - 1) return;
    tty->edit[tty->edit_count++] = c;
    if (lflag & TTY_ECHO) echo_char(tty, c);
}

static void receive(struct tty *tty, char c) {
    uint32_t iflag = tty->termios.iflag;
    if (iflag & TTY_ISTRIP) c &= 0x7f;
    if (c == '\r') {
        if (iflag & TTY_IGNCR) return;
        if (iflag & TTY_ICRNL) c = '\n';
    } else if (c == '\n' && (iflag & TTY_INLCR)) {
        c = '\r';
    }
    if ((tty->termios.lflag & TTY_ISIG) && !tty->literal && interrupt_char(tty, c)) return;
    if (tty->termios.lflag & TTY_ICANON) {
        canonical_char(tty, c);
        return;
    }
    push(tty, c, 0);
    if (tty->termios.lflag & TTY_ECHO) echo_char(tty, c);
    wake_up_all(&tty->readable);
}

void tty_input(char c) {
    unsigned long flags = spin_lock_irqsave(&console.lock);
    receive(&console, c);
    spin_unlock_irqrestore(&console.lock, flags);
}

static int background(struct tty *tty) {
    struct task *self = task_current();
    return self->ctty == tty && tty->pgrp && self->pgid != tty->pgrp;
}

static int job_check(struct tty *tty, int signal) {
    if (!background(tty)) return 0;
    struct task *self = task_current();
    if (signal_ignored_or_blocked(signal)) return signal == NARC_SIGTTIN ? -NARC_IO_ERROR : 0;
    if (task_group_orphaned(self->pgid)) return -NARC_IO_ERROR;
    task_signal_group(self->pgid, signal);
    return -NARC_INTERRUPTED;
}

static int readable(struct tty *tty, size_t minimum) {
    if (tty->termios.lflag & TTY_ICANON) return tty->lines > 0;
    return tty->count >= minimum;
}

static void wake_reader(struct ktimer *timer) {
    sched_wake_interruptible(timer->data);
}

static int wait_input(struct tty *tty, size_t minimum, uint64_t deadline, int nonblock) {
    struct waiter waiter = { 0 };
    struct ktimer timer = { .expire = wake_reader, .data = task_current() };
    if (deadline) ktimer_arm(&timer, deadline);
    int status = 0;
    for (;;) {
        wait_prepare_interruptible(&tty->readable, &waiter);
        unsigned long flags = spin_lock_irqsave(&tty->lock);
        int ready = readable(tty, minimum);
        spin_unlock_irqrestore(&tty->lock, flags);
        if (ready) break;
        if (nonblock) {
            status = -NARC_TRY_AGAIN;
            break;
        }
        if (deadline && time_monotonic_ns() >= deadline) break;
        if (signal_pending()) {
            status = -NARC_INTERRUPTED;
            break;
        }
        schedule();
    }
    wait_finish(&tty->readable, &waiter);
    if (deadline) ktimer_cancel(&timer);
    return status;
}

static size_t take(struct tty *tty, char *buffer, size_t length) {
    size_t done = 0;
    int canonical = (tty->termios.lflag & TTY_ICANON) != 0;
    while (done < length && tty->count) {
        char c = tty->queue[tty->head];
        uint8_t mark = tty->marks[tty->head];
        tty->head = (tty->head + 1) % TTY_BUFFER;
        tty->count--;
        if (mark) tty->lines--;
        if (mark != LINE_EOF) buffer[done++] = c;
        if (canonical && mark) break;
    }
    return done;
}

static long tty_read_locked(struct tty *tty, struct open_file *open, char *buffer,
                            size_t length) {
    int status = job_check(tty, NARC_SIGTTIN);
    if (status) return status;
    int nonblock = (open->flags & VFS_OPEN_NONBLOCK) != 0;
    size_t minimum = 1;
    uint64_t deadline = 0;
    if (!(tty->termios.lflag & TTY_ICANON)) {
        uint8_t vmin = tty->termios.cc[TTY_VMIN], vtime = tty->termios.cc[TTY_VTIME];
        minimum = vmin < length ? vmin : length;
        if (vtime) deadline = time_monotonic_ns() + vtime * (NSEC_PER_SEC / 10);
        if (!vmin && !vtime) nonblock = 1;
        if (!minimum) minimum = 1;
        if (vmin && vtime) {
            status = wait_input(tty, 1, 0, nonblock);
            if (status) return status;
            deadline = time_monotonic_ns() + vtime * (NSEC_PER_SEC / 10);
        }
    }
    status = wait_input(tty, minimum, deadline, nonblock);
    if (status == -NARC_TRY_AGAIN && !(open->flags & VFS_OPEN_NONBLOCK)) status = 0;
    if (status) return status;
    unsigned long flags = spin_lock_irqsave(&tty->lock);
    size_t done = take(tty, buffer, length);
    spin_unlock_irqrestore(&tty->lock, flags);
    return (long)done;
}

static long tty_read(struct open_file *open, void *buffer, size_t length) {
    struct tty *tty = open->private;
    mutex_lock(&tty->read_lock);
    long result = tty_read_locked(tty, open, buffer, length);
    mutex_unlock(&tty->read_lock);
    return result;
}

static long tty_write(struct open_file *open, const void *buffer, size_t length) {
    struct tty *tty = open->private;
    if (tty->termios.lflag & TTY_TOSTOP) {
        int status = job_check(tty, NARC_SIGTTOU);
        if (status) return status;
    }
    mutex_lock(&tty->write_lock);
    unsigned long flags = spin_lock_irqsave(&tty->lock);
    output(tty, buffer, length);
    spin_unlock_irqrestore(&tty->lock, flags);
    mutex_unlock(&tty->write_lock);
    return (long)length;
}

static int copy_out(uint64_t address, const void *data, size_t length) {
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, address, length, 1)) return -NARC_BAD_ADDRESS;
    return uaccess_write(space, address, data, length) ? -NARC_BAD_ADDRESS : 0;
}

static int copy_in(void *data, uint64_t address, size_t length) {
    struct address_space *space = vmm_space_current();
    if (!vmm_user_range_ok(space, address, length, 0)) return -NARC_BAD_ADDRESS;
    return uaccess_read(space, data, address, length) ? -NARC_BAD_ADDRESS : 0;
}

static int set_termios(struct tty *tty, uint32_t request, uint64_t argument) {
    narc_termios_t termios;
    int status = copy_in(&termios, argument, sizeof(termios));
    if (status) return status;
    status = job_check(tty, NARC_SIGTTOU);
    if (status) return status;
    unsigned long flags = spin_lock_irqsave(&tty->lock);
    int was_canonical = (tty->termios.lflag & TTY_ICANON) != 0;
    tty->termios = termios;
    if (request == NARC_TCSETSF) flush_input(tty);
    if (was_canonical && !(termios.lflag & TTY_ICANON)) {
        commit_line(tty, 0);
        tty->lines = 0;
        for (size_t i = 0; i < tty->count; i++) tty->marks[(tty->head + i) % TTY_BUFFER] = 0;
    }
    spin_unlock_irqrestore(&tty->lock, flags);
    wake_up_all(&tty->readable);
    return 0;
}

static int set_pgrp(struct tty *tty, uint64_t argument) {
    struct task *self = task_current();
    int pgrp;
    int status = copy_in(&pgrp, argument, sizeof(pgrp));
    if (status) return status;
    if (self->ctty != tty || tty->session != self->sid) return -NARC_NOT_A_TTY;
    if (pgrp <= 0) return -NARC_INVALID_ARGUMENT;
    if (task_group_session(pgrp) != self->sid) return -NARC_NOT_PERMITTED;
    status = job_check(tty, NARC_SIGTTOU);
    if (status) return status;
    tty->pgrp = pgrp;
    return 0;
}

static long tty_ioctl(struct open_file *open, uint32_t request, uint64_t argument) {
    struct tty *tty = open->private;
    struct task *self = task_current();
    switch (request) {
    case NARC_TCGETS:
        return copy_out(argument, &tty->termios, sizeof(tty->termios));
    case NARC_TCSETS:
    case NARC_TCSETSW:
    case NARC_TCSETSF:
        return set_termios(tty, request, argument);
    case NARC_TCFLSH: {
        unsigned long flags = spin_lock_irqsave(&tty->lock);
        if (argument != 1) flush_input(tty);
        spin_unlock_irqrestore(&tty->lock, flags);
        return 0;
    }
    case NARC_TIOCGPGRP: {
        if (self->ctty != tty) return -NARC_NOT_A_TTY;
        int pgrp = tty->pgrp;
        return copy_out(argument, &pgrp, sizeof(pgrp));
    }
    case NARC_TIOCSPGRP:
        return set_pgrp(tty, argument);
    case NARC_TIOCGSID: {
        if (self->ctty != tty || !tty->session) return -NARC_NOT_A_TTY;
        int session = tty->session;
        return copy_out(argument, &session, sizeof(session));
    }
    case NARC_TIOCGWINSZ:
        return copy_out(argument, &tty->winsize, sizeof(tty->winsize));
    case NARC_TIOCSWINSZ: {
        narc_winsize_t size;
        int status = copy_in(&size, argument, sizeof(size));
        if (status) return status;
        int changed = size.rows != tty->winsize.rows || size.cols != tty->winsize.cols;
        tty->winsize = size;
        if (changed && tty->pgrp) task_signal_group(tty->pgrp, NARC_SIGWINCH);
        return 0;
    }
    case NARC_FIONREAD: {
        unsigned long flags = spin_lock_irqsave(&tty->lock);
        int count = (int)tty->count;
        spin_unlock_irqrestore(&tty->lock, flags);
        return copy_out(argument, &count, sizeof(count));
    }
    case NARC_TIOCOUTQ: {
        int zero = 0;
        return copy_out(argument, &zero, sizeof(zero));
    }
    case NARC_TIOCSCTTY:
        if (self->pid != self->sid || self->ctty) return -NARC_NOT_PERMITTED;
        if (tty->session && tty->session != self->sid && argument != 1)
            return -NARC_NOT_PERMITTED;
        if (tty->session && tty->session != self->sid) task_detach_tty(tty, tty->session);
        tty->session = self->sid;
        tty->pgrp = self->pgid;
        task_attach_tty(tty, self->sid);
        return 0;
    case NARC_TIOCNOTTY:
        if (self->ctty != tty) return -NARC_NOT_A_TTY;
        if (self->pid == self->sid) tty_session_exit(tty, self->sid);
        else self->ctty = 0;
        return 0;
    default:
        return -NARC_NOT_A_TTY;
    }
}

void tty_session_exit(struct tty *tty, int session) {
    if (!tty || tty->session != session) return;
    int pgrp = tty->pgrp;
    tty->session = tty->pgrp = 0;
    task_detach_tty(tty, session);
    if (pgrp > 0) {
        task_signal_group(pgrp, NARC_SIGHUP);
        task_signal_group(pgrp, NARC_SIGCONT);
    }
}

static const struct file_ops tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .ioctl = tty_ioctl,
};

struct open_file *tty_open(void) {
    struct open_file *open = file_alloc(&tty_ops, &console);
    if (open) open->flags = VFS_OPEN_READ | VFS_OPEN_WRITE;
    return open;
}

static int open_console(struct char_device *device, uint32_t flags, struct open_file **result) {
    (void)device;
    struct task *self = task_current();
    if (!(flags & VFS_OPEN_NOCTTY) && self->pid == self->sid && !self->ctty && !console.session) {
        console.session = self->sid;
        console.pgrp = self->pgid;
        task_attach_tty(&console, self->sid);
    }
    *result = file_alloc(&tty_ops, &console);
    return *result ? 0 : -NARC_NO_MEMORY;
}

static int open_controlling(struct char_device *device, uint32_t flags, struct open_file **result) {
    (void)device;
    (void)flags;
    struct tty *tty = task_current()->ctty;
    if (!tty) return -NARC_NO_DEVICE;
    *result = file_alloc(&tty_ops, tty);
    return *result ? 0 : -NARC_NO_MEMORY;
}

static struct char_device tty_devices[] = {
    { 0, DEVICE_MAKE(5, 0), "tty", open_controlling, 0 },
    { 0, DEVICE_MAKE(5, 1), "console", open_console, 0 },
};

void tty_devices_init(void) {
    device_register(&tty_devices[0]);
    device_register(&tty_devices[1]);
}

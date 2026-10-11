#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/mutex.h>
#include <kernel/serial.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/tty.h>
#include <kernel/wait.h>

#define TTY_RING 1024
#define TTY_LINE 256

static struct spinlock ring_lock = SPINLOCK_INIT;
static char ring[TTY_RING];
static size_t ring_head, ring_count;
static struct wait_queue input_wait = WAIT_QUEUE_INIT;
static struct mutex read_lock = MUTEX_INIT;
static char line[TTY_LINE];
static size_t line_pos, line_count;
static int line_ready;

void tty_init(void) {
    serial_rx_enable();
    arch_serial_irq_init();
}

void tty_input(char c) {
    unsigned long flags = spin_lock_irqsave(&ring_lock);
    if (ring_count < TTY_RING) ring[(ring_head + ring_count++) % TTY_RING] = c;
    spin_unlock_irqrestore(&ring_lock, flags);
    wake_up_all(&input_wait);
}

static int pop_input(char *c) {
    unsigned long flags = spin_lock_irqsave(&ring_lock);
    int available = ring_count != 0;
    if (available) {
        *c = ring[ring_head];
        ring_head = (ring_head + 1) % TTY_RING;
        ring_count--;
    }
    spin_unlock_irqrestore(&ring_lock, flags);
    return available;
}

static int read_line(void) {
    while (line_count < sizeof(line)) {
        char c;
        int status = wait_event_interruptible(&input_wait, pop_input(&c));
        if (status) return status;
        if (c == '\r') c = '\n';
        if (c == '\b' || c == 127) {
            if (line_count) {
                line_count--;
                console_write("\b \b", 3);
            }
            continue;
        }
        line[line_count++] = c;
        console_write(&c, 1);
        if (c == '\n') break;
    }
    return 0;
}

static long tty_read(struct open_file *open, void *buffer, size_t length) {
    (void)open;
    mutex_lock(&read_lock);
    if (!line_ready) {
        int status = read_line();
        if (status) {
            mutex_unlock(&read_lock);
            return status;
        }
        line_ready = 1;
        line_pos = 0;
    }
    size_t done = length < line_count - line_pos ? length : line_count - line_pos;
    memcpy(buffer, line + line_pos, done);
    line_pos += done;
    if (line_pos == line_count) line_ready = line_count = 0;
    mutex_unlock(&read_lock);
    return (long)done;
}

static long tty_write(struct open_file *open, const void *buffer, size_t length) {
    (void)open;
    console_write(buffer, length);
    return (long)length;
}

static const struct file_ops tty_ops = { .read = tty_read, .write = tty_write };

struct open_file *tty_open(void) {
    return file_alloc(&tty_ops, 0);
}

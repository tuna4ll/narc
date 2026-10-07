#include <kernel/console.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/tty.h>

static char input[256];
static size_t input_pos, input_count;

static void read_line(void) {
    input_pos = input_count = 0;
    while (input_count < sizeof(input)) {
        char c = serial_getc();
        if (c == '\r') c = '\n';
        if (c == '\b' || c == 127) {
            if (input_count) {
                input_count--;
                console_write("\b \b", 3);
            }
            continue;
        }
        input[input_count++] = c;
        console_write(&c, 1);
        if (c == '\n') break;
    }
}

static long tty_read(struct open_file *open, void *buffer, size_t length) {
    (void)open;
    if (input_pos == input_count) read_line();
    size_t done = length < input_count - input_pos ? length : input_count - input_pos;
    memcpy(buffer, input + input_pos, done);
    input_pos += done;
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

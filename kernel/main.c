#include <kernel/arch.h>
#include <kernel/heap.h>
#include <kernel/limine.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/serial.h>
#include <kernel/user.h>
#include <kernel/vfs.h>
#include <kernel/console.h>
#include <stdint.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

__attribute__((noreturn))
void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision) ||
        !memmap_request.response || !hhdm_request.response || !framebuffer_request.response ||
        !module_request.response || module_request.response->module_count != 1) {
        arch_halt();
    }

    mm_init(memmap_request.response, hhdm_request.response->offset);
    arch_init((uint64_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack)));
    heap_init();
    sched_init((uint64_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack)));
    serial_init();
    if (console_init(framebuffer_request.response) != 0) {
        arch_halt();
    }
    console_puts("[boot] Limine framebuffer ready\n");
    struct limine_file *initramfs = module_request.response->modules[0];
    if (vfs_init(initramfs->address, initramfs->size) != 0) {
        console_puts("[panic] invalid initramfs\n");
        arch_halt();
    }

    console_puts("[kernel] ring 0 initialized\n");

    user_start();
    sched_idle();
}

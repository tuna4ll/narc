#include <stdint.h>
#include <stddef.h>

#include "usermode.h"
#include "gdt.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "terminal.h"
#include "cpu.h"

__asm__(
    ".section .rodata\n"
    "user_program_start:\n"
    ".incbin \"build/user/hello.bin\"\n"
    "user_program_end:\n"
    ".previous\n"
);

extern uint8_t user_program_start[];
extern uint8_t user_program_end[];

static void map_user_page(uint64_t virt, uint64_t flags) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0 || !vmm_map_page(virt, phys, flags | PTE_USER)) {
        kprint("USER: could not map page!\n");
        khalt();
    }
}

static void jump_to_usermode(uint64_t entry, uint64_t stack) {
    __asm__ volatile (
        "pushq $0x1B\n"
        "pushq %0\n"
        "pushq $0x202\n"
        "pushq $0x23\n"
        "pushq %1\n"
        "iretq\n"
        : : "r"(stack), "r"(entry) : "memory"
    );
}

void usermode_start(void) {
    uint64_t size = user_program_end - user_program_start;
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++)
        map_user_page(USER_CODE_ADDR + i * PAGE_SIZE, PTE_WRITE);

    for (uint64_t i = 1; i <= USER_STACK_PAGES; i++)
        map_user_page(USER_STACK_TOP - i * PAGE_SIZE, PTE_WRITE | PTE_NX);

    memset((void *)USER_CODE_ADDR, 0, pages * PAGE_SIZE);
    memcpy((void *)USER_CODE_ADDR, user_program_start, size);

    kprint("USER: program is ");
    kprint_dec(size);
    kprint(" bytes, jumping to userspace...\n");

    jump_to_usermode(USER_CODE_ADDR, USER_STACK_TOP - 8);
}

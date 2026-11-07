#include <stdint.h>
#include <stddef.h>

#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "terminal.h"
#include "kernel.h"

static struct process processes[MAX_PROCESSES];
static struct process *current = NULL;
static int next_pid = 1;

static void print_signed(int64_t n) {
    if (n < 0) {
        kprint("-");
        n = -n;
    }
    kprint_dec(n);
}

static uint64_t map_new_page(uint64_t pml4, uint64_t virt, uint64_t flags) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0)
        return 0;

    memset(vmm_phys_to_virt(phys), 0, PAGE_SIZE);

    if (!vmm_map_page_in(pml4, virt, phys, flags | PTE_USER)) {
        pmm_free_page(phys);
        return 0;
    }
    return phys;
}

struct process *process_create(const char *name, const uint8_t *code, uint64_t size) {
    struct process *p = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            p = &processes[i];
            break;
        }
    }
    if (p == NULL)
        return NULL;

    uint64_t pml4 = vmm_new_address_space();
    if (pml4 == 0)
        return NULL;

    uint64_t first_phys = 0;

    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        uint64_t phys = map_new_page(pml4, USER_CODE_ADDR + off, PTE_WRITE);
        if (phys == 0) {
            vmm_free_address_space(pml4);
            return NULL;
        }
        if (off == 0)
            first_phys = phys;

        uint64_t n = size - off < PAGE_SIZE ? size - off : PAGE_SIZE;
        memcpy(vmm_phys_to_virt(phys), code + off, n);
    }

    for (uint64_t i = 1; i <= USER_STACK_PAGES; i++) {
        if (map_new_page(pml4, USER_STACK_TOP - i * PAGE_SIZE, PTE_WRITE | PTE_NX) == 0) {
            vmm_free_address_space(pml4);
            return NULL;
        }
    }

    p->pid = next_pid++;
    p->name = name;
    p->pml4 = pml4;
    p->state = PROC_READY;

    kprint("PROC: created pid ");
    kprint_dec(p->pid);
    kprint(" (");
    kprint(name);
    kprint(") cr3 ");
    kprint_hex(pml4);
    kprint(" code phys ");
    kprint_hex(first_phys);
    kprint("\n");

    return p;
}

struct process *process_current(void) {
    return current;
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

void process_start_next(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_READY) {
            current = &processes[i];
            current->state = PROC_RUNNING;
            vmm_write_cr3(current->pml4);
            jump_to_usermode(USER_CODE_ADDR, USER_STACK_TOP - 8);
        }
    }

    current = NULL;
    kprint("PROC: no processes left, free pages ");
    kprint_dec(pmm_get_free_pages());
    kprint("\n");
    kernel_idle();
}

void process_exit(int64_t code) {
    struct process *p = current;

    kprint("PROC: pid ");
    kprint_dec(p->pid);
    kprint(" (");
    kprint(p->name);
    kprint(") exited with code ");
    print_signed(code);
    kprint("\n");

    vmm_write_cr3(vmm_kernel_space());
    vmm_free_address_space(p->pml4);
    p->state = PROC_UNUSED;
    current = NULL;

    process_start_next();
}

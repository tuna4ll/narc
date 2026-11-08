#include <stdint.h>
#include <stddef.h>

#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "kprint.h"
#include "kernel.h"
#include "heap.h"
#include "tss.h"

static struct process processes[MAX_PROCESSES];
static struct process *current = NULL;
static struct process idle;
static int next_pid = 1;
static uint64_t slice_used = 0;

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

    uint8_t *kstack = kmalloc(KERNEL_STACK_SIZE);
    if (kstack == NULL) {
        vmm_free_address_space(pml4);
        return NULL;
    }

    uint64_t *frame = (uint64_t *)(kstack + KERNEL_STACK_SIZE);
    *--frame = 0x1B;
    *--frame = USER_STACK_TOP - 8;
    *--frame = 0x202;
    *--frame = 0x23;
    *--frame = USER_CODE_ADDR;
    for (int i = 0; i < 15; i++)
        *--frame = 0;

    p->pid = next_pid++;
    p->name = name;
    p->pml4 = pml4;
    p->kernel_stack = kstack;
    p->rsp = (uint64_t)frame;
    p->state = PROC_READY;

    kprint("PROC: created pid %d (%s) cr3 0x%lx code phys 0x%lx\n", p->pid, name, pml4, first_phys);

    return p;
}

static void free_dead_processes(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &processes[i];
        if (p->state == PROC_DEAD && p != current) {
            vmm_free_address_space(p->pml4);
            kfree(p->kernel_stack);
            p->state = PROC_UNUSED;
        }
    }
}

uint64_t schedule(uint64_t rsp) {
    if (current == NULL)
        return rsp;

    current->rsp = rsp;
    if (current->state == PROC_RUNNING)
        current->state = PROC_READY;

    free_dead_processes();

    int start = 0;
    if (current != &idle)
        start = (current - processes) + 1;

    struct process *next = &idle;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &processes[(start + i) % MAX_PROCESSES];
        if (p->state == PROC_READY) {
            next = p;
            break;
        }
    }

    current = next;
    current->state = PROC_RUNNING;
    slice_used = 0;

    if (next != &idle)
        tss_set_rsp0((uint64_t)next->kernel_stack + KERNEL_STACK_SIZE);
    vmm_write_cr3(next->pml4);

    return next->rsp;
}

uint64_t scheduler_tick(uint64_t rsp) {
    if (current == NULL)
        return rsp;
    if (current != &idle && current->state == PROC_RUNNING && ++slice_used < QUANTUM_TICKS)
        return rsp;
    return schedule(rsp);
}

void scheduler_start(void) {
    idle.pid = 0;
    idle.name = "idle";
    idle.pml4 = vmm_kernel_space();
    idle.state = PROC_RUNNING;
    current = &idle;

    kprint("SCHED: starting\n");
    kernel_idle();
}

void process_exit(int64_t code) {
    kprint("PROC: pid %d (%s) exited with code %ld\n", current->pid, current->name, code);

    current->state = PROC_DEAD;

    for (;;)
        __asm__ volatile ("sti; hlt");
}

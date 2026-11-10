#include <stdbool.h>
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
#include "idt.h"
#include "initramfs.h"

#define KSTACK_REGION 0xFFFFB00000000000ull
#define KSTACK_PAGES (KERNEL_STACK_SIZE / PAGE_SIZE)
#define MS_PER_TICK 10

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define ET_EXEC 2
#define EM_X86_64 62
#define USER_IMAGE_END (USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE)

struct elf_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf_segment {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

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

static bool load_segment(uint64_t pml4, const uint8_t *image, const struct elf_segment *s, uint64_t start, uint64_t end) {
    uint64_t flags = 0;
    if (s->flags & PF_W)
        flags |= PTE_WRITE;
    if (!(s->flags & PF_X))
        flags |= PTE_NX;

    uint64_t file_end = s->vaddr + s->filesz;

    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
        uint64_t phys = map_new_page(pml4, page, flags);
        if (phys == 0)
            return false;

        uint64_t from = page < s->vaddr ? s->vaddr : page;
        uint64_t to = page + PAGE_SIZE < file_end ? page + PAGE_SIZE : file_end;
        if (from < to)
            memcpy((uint8_t *)vmm_phys_to_virt(phys) + (from - page), image + s->offset + (from - s->vaddr), to - from);
    }
    return true;
}

static bool load_elf(uint64_t pml4, const uint8_t *image, uint64_t size, uint64_t *entry) {
    if (size < sizeof(struct elf_header))
        return false;

    const struct elf_header *h = (const struct elf_header *)image;
    if (memcmp(h->ident, "\x7f" "ELF\x02\x01", 6) != 0
        || h->type != ET_EXEC
        || h->machine != EM_X86_64
        || h->phentsize != sizeof(struct elf_segment)
        || h->phoff > size
        || h->phnum * sizeof(struct elf_segment) > size - h->phoff) {
        return false;
    }

    const struct elf_segment *segments = (const struct elf_segment *)(image + h->phoff);
    uint64_t next_free = USER_CODE_ADDR;
    bool entry_ok = false;

    for (uint16_t i = 0; i < h->phnum; i++) {
        const struct elf_segment *s = &segments[i];
        if (s->type != PT_LOAD || s->memsz == 0)
            continue;

        if (s->filesz > s->memsz
            || s->offset > size
            || s->filesz > size - s->offset
            || s->vaddr >= USER_IMAGE_END
            || s->memsz > USER_IMAGE_END - s->vaddr) {
            return false;
        }

        uint64_t start = s->vaddr & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t end = (s->vaddr + s->memsz + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
        if (start < next_free || !load_segment(pml4, image, s, start, end))
            return false;
        next_free = end;

        if ((s->flags & PF_X) && h->entry >= s->vaddr && h->entry < s->vaddr + s->memsz)
            entry_ok = true;
    }

    *entry = h->entry;
    return entry_ok;
}

static void kstack_free(uint8_t *stack) {
    for (uint64_t i = 0; i < KSTACK_PAGES; i++) {
        uint64_t phys = vmm_unmap_page((uint64_t)stack + i * PAGE_SIZE);
        if (phys != 0)
            pmm_free_page(phys);
    }
}

static uint8_t *kstack_alloc(int slot) {
    uint8_t *stack = (uint8_t *)(KSTACK_REGION + (slot * (KSTACK_PAGES + 1) + 1) * PAGE_SIZE);

    for (uint64_t i = 0; i < KSTACK_PAGES; i++) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            kstack_free(stack);
            return NULL;
        }
        if (!vmm_map_page((uint64_t)stack + i * PAGE_SIZE, phys, PTE_WRITE | PTE_NX)) {
            pmm_free_page(phys);
            kstack_free(stack);
            return NULL;
        }
    }
    return stack;
}

struct process *process_create(const char *name, const uint8_t *image, uint64_t size) {
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

    uint64_t entry;
    if (!load_elf(pml4, image, size, &entry)) {
        vmm_free_address_space(pml4);
        return NULL;
    }

    for (uint64_t i = 1; i <= USER_STACK_PAGES; i++) {
        if (map_new_page(pml4, USER_STACK_TOP - i * PAGE_SIZE, PTE_WRITE | PTE_NX) == 0) {
            vmm_free_address_space(pml4);
            return NULL;
        }
    }

    uint8_t *kstack = kstack_alloc(p - processes);
    if (kstack == NULL) {
        vmm_free_address_space(pml4);
        return NULL;
    }

    uint64_t *frame = (uint64_t *)(kstack + KERNEL_STACK_SIZE);
    *--frame = 0x1B;
    *--frame = USER_STACK_TOP - 8;
    *--frame = 0x202;
    *--frame = 0x23;
    *--frame = entry;
    for (int i = 0; i < 15; i++)
        *--frame = 0;

    int n = 0;
    for (; n < PROC_NAME_LEN - 1 && name[n] != '\0'; n++)
        p->name[n] = name[n];
    p->name[n] = '\0';

    p->pid = next_pid++;
    p->pml4 = pml4;
    p->kernel_stack = kstack;
    p->rsp = (uint64_t)frame;
    p->parent = NULL;
    p->exit_code = 0;
    p->state = PROC_READY;

    kprint("PROC: created pid %d (%s) cr3 0x%lx entry 0x%lx\n", p->pid, name, pml4, entry);

    return p;
}

int process_spawn(const char *name) {
    const uint8_t *image;
    uint64_t size;

    if (!initramfs_find(name, &image, &size))
        return -1;

    struct process *p = process_create(name, image, size);
    if (p == NULL)
        return -1;

    p->parent = current;
    return p->pid;
}

int process_wait(int pid, int *exit_code) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &processes[i];
        if (p->parent != current || p->pid != pid)
            continue;

        if (p->state != PROC_DEAD && p->state != PROC_ZOMBIE) {
            current->state = PROC_WAITING;
            current->wait_pid = pid;
            return 0;
        }

        *exit_code = p->exit_code;
        p->parent = NULL;
        if (p->state == PROC_ZOMBIE)
            p->state = PROC_UNUSED;
        return 1;
    }
    return -1;
}

void process_sleep(uint64_t ms) {
    current->wake_tick = timer_get_ticks() + ms / MS_PER_TICK + 1;
    current->state = PROC_SLEEPING;
}

static void free_dead_processes(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &processes[i];
        if (p->state == PROC_DEAD && p != current) {
            vmm_free_address_space(p->pml4);
            kstack_free(p->kernel_stack);
            p->state = p->parent != NULL ? PROC_ZOMBIE : PROC_UNUSED;
        }
    }
}

static void wake_sleepers(void) {
    uint64_t now = timer_get_ticks();

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &processes[i];
        if (p->state == PROC_SLEEPING && now >= p->wake_tick)
            p->state = PROC_READY;
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
    wake_sleepers();
    if (current != &idle && current->state == PROC_RUNNING && ++slice_used < QUANTUM_TICKS)
        return rsp;
    return schedule(rsp);
}

void scheduler_start(void) {
    idle.pid = 0;
    memcpy(idle.name, "idle", 5);
    idle.pml4 = vmm_kernel_space();
    idle.state = PROC_RUNNING;
    current = &idle;

    kprint("SCHED: starting\n");
    kernel_idle();
}

void process_exit(int64_t code) {
    kprint("PROC: pid %d (%s) exited with code %ld\n", current->pid, current->name, code);

    current->exit_code = (int)code;
    current->state = PROC_DEAD;

    struct process *parent = current->parent;
    if (parent != NULL && parent->state == PROC_WAITING && parent->wait_pid == current->pid)
        parent->state = PROC_READY;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &processes[i];
        if (p->state != PROC_UNUSED && p->parent == current) {
            p->parent = NULL;
            if (p->state == PROC_ZOMBIE)
                p->state = PROC_UNUSED;
        }
    }

    for (;;)
        __asm__ volatile ("sti; hlt");
}

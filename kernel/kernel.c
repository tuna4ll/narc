#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "serial.h"
#include "terminal.h"
#include "kprint.h"
#include "pit.h"
#include "idt.h"
#include "pic.h"
#include "pmm.h"
#include "boot.h"
#include "vmm.h"
#include "heap.h"
#include "gdt.h"
#include "tss.h"
#include "kernel.h"
#include "process.h"
#include "random.h"
#include "initramfs.h"

static int not_heap;

#define SLOTS 256

static char *slot_ptr[SLOTS];
static size_t slot_size[SLOTS];

static void fill(int i) {
    for (size_t j = 0; j < slot_size[i]; j++)
        slot_ptr[i][j] = (char)(i + j);
}

static bool verify(int i) {
    for (size_t j = 0; j < slot_size[i]; j++)
        if (slot_ptr[i][j] != (char)(i + j))
            return false;
    return true;
}

static bool heap_stress(int rounds) {
    for (int r = 0; r < rounds; r++) {
        int i = krand() % SLOTS;
        size_t size = 1 + krand() % 4096;
        if (krand() % 50 == 0)
            size = 1 + krand() % (256 * 1024);

        if (slot_ptr[i] == NULL) {
            slot_ptr[i] = kmalloc(size);
            if (!slot_ptr[i])
                return false;
            slot_size[i] = size;
            fill(i);
        } else if (krand() % 3 == 0) {
            if (!verify(i))
                return false;
            size_t keep = slot_size[i] < size ? slot_size[i] : size;
            slot_ptr[i] = krealloc(slot_ptr[i], size);
            if (!slot_ptr[i])
                return false;
            slot_size[i] = keep;
            if (!verify(i))
                return false;
            slot_size[i] = size;
            fill(i);
        } else {
            if (!verify(i))
                return false;
            kfree(slot_ptr[i]);
            slot_ptr[i] = NULL;
        }

        if (r % 1000 == 0 && !heap_check())
            return false;
    }

    for (int i = 0; i < SLOTS; i++) {
        if (slot_ptr[i]) {
            if (!verify(i))
                return false;
            kfree(slot_ptr[i]);
            slot_ptr[i] = NULL;
        }
    }
    return heap_check();
}

static void start_program(const char *name) {
    if (process_spawn(name) < 0)
        kprint("PROC: could not start %s\n", name);
}

void kmain(void) {
    serial_init();
    kprint("Narc: booting\n");

    struct boot_info boot;

    if (!boot_init(&boot))
        panic("boot_init failed");

    if (!pmm_init(boot.memmap, boot.memmap_count, boot.hhdm_offset))
        panic("pmm_init failed");

    uint64_t limine_cr3 = vmm_read_cr3();
    uint64_t free_before = pmm_get_free_pages();

    if (!vmm_init(&boot))
        panic("vmm_init failed");

    uint64_t vmm_pages = free_before - pmm_get_free_pages();

    if (!heap_init())
        panic("heap_init failed");

    terminal_init(&boot.framebuffer);
    kprint("Hello World\n");

    struct heap_stats st;
    heap_get_stats(&st);
    kprint("HEAP: terminal is using %lu bytes, heap mapped %lu\n", st.used, st.mapped);

    kprint("PMM memory map initialized!\n");

    uint64_t page = pmm_alloc_page();

    if (page == 0)
        panic("PMM: allocation failed");

    kprint("PMM: physical page allocated!\n");

    uint64_t hhdm = boot.hhdm_offset;

    volatile uint8_t *memory =
        (volatile uint8_t *)(uintptr_t)(page + hhdm);

    memory[0] = 42;

    if (memory[0] == 42) {
        kprint("PMM: read/write successful!\n");
    } else {
        panic("PMM: read/write failed");
    }

    pmm_free_page(page);

    kprint("PMM: physical page freed!\n");

    kprint("Narc\n");

    kprint("      %%%%%%%%%%@%%##     \n");
    kprint(" *+   %%%%%%@%%#@%%#%%    \n");
    kprint(" +-=  @%%++++*%%%%@    \n");
    kprint(" #*#@ @*#%%=*+*@@    \n");
    kprint("   #*###****=-+%%   \n") ;
    kprint("  %%++==:=**+-:-.-+\n")  ;
    kprint("   -::-:-=:.-----:*\n") ;
    kprint("  %%%%*=----..-**+.=*\n") ;
    kprint(" @###*:-..:-=*##-+%%\n") ;
    kprint("  ###*+==*:-**#*#%%\n")  ;
    kprint("   %%##@    %%####\n\n\n")    ;

    uint16_t old_cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(old_cs));

    gdt_init();

    uint16_t new_cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(new_cs));
    kprint("CS before: 0x%x CS after: 0x%x\n", old_cs, new_cs);

    tss_init();

    idt_init();
    pic_remap();
    pit_init(100);

    pic_clear_mask(0);
    pic_clear_mask(1);

    kprint("Testing INT3...\n");

    __asm__ volatile ("int3");

    kprint("Interrupt returned!\n");
    kprint("Enabling interrupts...\n");

    __asm__ volatile ("sti");

    kprint("VMM: limine CR3 = 0x%lx\n", limine_cr3);
    kprint("VMM: narc CR3   = 0x%lx\n", vmm_read_cr3());
    kprint("VMM: page tables used %lu pages\n", vmm_pages);
    kprint("VMM: still alive after CR3 switch :D\n");

    uint64_t test_virt = (uint64_t)&kmain;
    kprint("VMM: kmain virt = 0x%lx phys = 0x%lx\n", test_virt, vmm_virt_to_phys(test_virt));

    uint64_t test_page = pmm_alloc_page();
    kprint("VMM: pmm page = 0x%lx hhdm walk = 0x%lx\n", test_page, vmm_virt_to_phys(test_page + boot.hhdm_offset));
    pmm_free_page(test_page);

    uint64_t my_virt = 0xFFFF900000000000;
    uint64_t free_map = pmm_get_free_pages();
    uint64_t my_phys = pmm_alloc_page();

    if (!vmm_map_page(my_virt, my_phys, PTE_WRITE))
        panic("VMM: map failed");

    kprint("VMM: mapped 0x%lx -> 0x%lx\n", my_virt, vmm_virt_to_phys(my_virt));

    volatile uint32_t *p1 = (volatile uint32_t *)my_virt;
    volatile uint32_t *p2 = (volatile uint32_t *)(my_phys + boot.hhdm_offset);

    *p1 = 0x4E415243;

    kprint("VMM: write via my_virt, read via hhdm = 0x%x\n", *p2);

    if (*p2 == 0x4E415243)
        kprint("VMM: SAME PAGE!!! map works\n");

    uint64_t freed = vmm_unmap_page(my_virt);
    pmm_free_page(freed);

    kprint("VMM: unmapped, walk now = 0x%lx\n", vmm_virt_to_phys(my_virt));

    kprint("VMM: free pages before map = %lu after unmap = %lu\n", free_map, pmm_get_free_pages());

    char *a = kmalloc(100);
    char *b = kmalloc(200);
    char *c = kmalloc(5000);

    kprint("HEAP: a = %p\n", a);
    kprint("HEAP: b = %p\n", b);
    kprint("HEAP: c = %p\n", c);

    kfree(b);
    char *d = kmalloc(150);
    kprint("HEAP: freed b, d = %p\n", d);

    if (d == b)
        kprint("HEAP: d got b's spot!\n");

    char *p[10];
    for (int i = 0; i < 10; i++)
        p[i] = kmalloc(1000);
    for (int i = 0; i < 10; i++)
        kfree(p[i]);

    char *big = kmalloc(9000);
    kprint("HEAP: p[0] = %p big = %p\n", p[0], big);
    if (big == p[0])
        kprint("HEAP: big reused the 10 freed blocks!\n");
    else
        kprint("HEAP: big did NOT fit in the freed blocks :(\n");

    char *r = kmalloc(16);
    for (int i = 0; i < 16; i++)
        r[i] = 'A' + i;
    r = krealloc(r, 20000);
    bool same = true;
    for (int i = 0; i < 16; i++)
        if (r[i] != 'A' + i)
            same = false;
    kprint("HEAP: krealloc %s the data\n", same ? "kept" : "LOST");

    uint32_t *z = kcalloc(1000, sizeof(uint32_t));
    bool zero = true;
    for (int i = 0; i < 1000; i++)
        if (z[i] != 0)
            zero = false;
    kprint(zero ? "HEAP: kcalloc gave zeroed memory\n" : "HEAP: kcalloc memory NOT zero\n");

    kfree(z);
    kfree(z);
    kfree((void *)0xFFFFC00000000123);
    kfree(&not_heap);

    kfree(r);
    kfree(big);
    kfree(a);
    kfree(c);
    kfree(d);

    heap_get_stats(&st);
    kprint("HEAP: mapped %lu used %lu free %lu blocks %lu\n", st.mapped, st.used, st.free, st.blocks);
    kprint("HEAP: heap_check %s\n", heap_check() ? "OK" : "FAILED");

    kprint("HEAP: stress test 20000 rounds...\n");
    uint64_t pmm_before = pmm_get_free_pages();
    uint64_t t0 = timer_get_ticks();
    bool stress_ok = heap_stress(20000);
    uint64_t t1 = timer_get_ticks();
    kprint("HEAP: stress test %s in %lu ms\n", stress_ok ? "PASSED" : "FAILED", (t1 - t0) * 10);

    heap_get_stats(&st);
    kprint("HEAP: mapped %lu used %lu free %lu blocks %lu\n", st.mapped, st.used, st.free, st.blocks);
    kprint("HEAP: pmm free pages before %lu after %lu\n", pmm_before, pmm_get_free_pages());

    initramfs_init(boot.initramfs, boot.initramfs_size);

    static const uint8_t bad_elf[64];
    kprint("ELF: bad image %s\n", process_create("bad", bad_elf, sizeof(bad_elf)) ? "ACCEPTED" : "rejected");

    start_program("sh");

    scheduler_start();
}

void kernel_idle(void) {
    __asm__ volatile ("sti");

    for (;;)
        __asm__ volatile ("hlt");
}

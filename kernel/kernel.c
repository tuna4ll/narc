#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard.h"
#include "io.h"
#include "cpu.h"
#include "terminal.h"
#include "pit.h"
#include "idt.h"
#include "pic.h"
#include "pmm.h"
#include "boot.h"
#include "vmm.h"
#include "heap.h"
#include "random.h"

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

void kmain(void) {
    outb(0xE9, 'K');

    struct boot_info boot;

    if (!boot_init(&boot)) {
        outb(0xE9, 'E');
        khalt();
    }

    if (!pmm_init(boot.memmap, boot.memmap_count, boot.hhdm_offset)) {
        outb(0xE9, 'P');
        khalt();
    }

    uint64_t limine_cr3 = vmm_read_cr3();
    uint64_t free_before = pmm_get_free_pages();

    if (!vmm_init(&boot)) {
        outb(0xE9, 'V');
        khalt();
    }

    uint64_t vmm_pages = free_before - pmm_get_free_pages();

    if (!heap_init()) {
        outb(0xE9, 'H');
        khalt();
    }

    terminal_init(&boot.framebuffer);
    kprint("Hello World\n");

    struct heap_stats st;
    heap_get_stats(&st);
    kprint("HEAP: terminal is using "); kprint_dec(st.used);
    kprint(" bytes, heap mapped "); kprint_dec(st.mapped); kprint("\n");

    kprint("PMM memory map initialized!\n");

    uint64_t page = pmm_alloc_page();

    if (page == 0) {
        kprint("PMM: allocation failed!\n");
        khalt();
    }

    kprint("PMM: physical page allocated!\n");

    uint64_t hhdm = boot.hhdm_offset;

    volatile uint8_t *memory =
        (volatile uint8_t *)(uintptr_t)(page + hhdm);

    memory[0] = 42;

    if (memory[0] == 42) {
        kprint("PMM: read/write successful!\n");
    } else {
        kprint("PMM: read/write failed!\n");
        khalt();
    }

    pmm_free_page(page);

    kprint("PMM: physical page freed!\n");

    kprint("Narc\n");

    kprint("      %%%%%@%##     \n");
    kprint(" *+   %%%@%#@%#%    \n");
    kprint(" +-=  @%++++*%%@    \n");
    kprint(" #*#@ @*#%=*+*@@    \n");
    kprint("   #*###****=-+%   \n") ;
    kprint("  %++==:=**+-:-.-+\n")  ;
    kprint("   -::-:-=:.-----:*\n") ;
    kprint("  %%*=----..-**+.=*\n") ;
    kprint(" @###*:-..:-=*##-+%\n") ;
    kprint("  ###*+==*:-**#*#%\n")  ;
    kprint("   %##@    %####\n\n\n")    ;

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

    kprint("VMM: limine CR3 = ");
    kprint_hex(limine_cr3);
    kprint("\n");
    kprint("VMM: narc CR3   = ");
    kprint_hex(vmm_read_cr3());
    kprint("\n");
    kprint("VMM: page tables used ");
    kprint_dec(vmm_pages);
    kprint(" pages\n");
    kprint("VMM: still alive after CR3 switch :D\n");

    uint64_t test_virt = (uint64_t)&kmain;
    kprint("VMM: kmain virt = ");
    kprint_hex(test_virt);
    kprint(" phys = ");
    kprint_hex(vmm_virt_to_phys(test_virt));
    kprint("\n");

    uint64_t test_page = pmm_alloc_page();
    kprint("VMM: pmm page = ");
    kprint_hex(test_page);
    kprint(" hhdm walk = ");
    kprint_hex(vmm_virt_to_phys(test_page + boot.hhdm_offset));
    kprint("\n");
    pmm_free_page(test_page);

    uint64_t my_virt = 0xFFFF900000000000;
    uint64_t free_map = pmm_get_free_pages();
    uint64_t my_phys = pmm_alloc_page();

    if (!vmm_map_page(my_virt, my_phys, PTE_WRITE)) {
        kprint("VMM: map failed!\n");
        khalt();
    }

    kprint("VMM: mapped ");
    kprint_hex(my_virt);
    kprint(" -> ");
    kprint_hex(vmm_virt_to_phys(my_virt));
    kprint("\n");

    volatile uint32_t *p1 = (volatile uint32_t *)my_virt;
    volatile uint32_t *p2 = (volatile uint32_t *)(my_phys + boot.hhdm_offset);

    *p1 = 0x4E415243;

    kprint("VMM: write via my_virt, read via hhdm = ");
    kprint_hex(*p2);
    kprint("\n");

    if (*p2 == 0x4E415243)
        kprint("VMM: SAME PAGE!!! map works\n");

    uint64_t freed = vmm_unmap_page(my_virt);
    pmm_free_page(freed);

    kprint("VMM: unmapped, walk now = ");
    kprint_hex(vmm_virt_to_phys(my_virt));
    kprint("\n");

    kprint("VMM: free pages before map = ");
    kprint_dec(free_map);
    kprint(" after unmap = ");
    kprint_dec(pmm_get_free_pages());
    kprint("\n");

    char *a = kmalloc(100);
    char *b = kmalloc(200);
    char *c = kmalloc(5000);

    kprint("HEAP: a = "); kprint_hex((uint64_t)a); kprint("\n");
    kprint("HEAP: b = "); kprint_hex((uint64_t)b); kprint("\n");
    kprint("HEAP: c = "); kprint_hex((uint64_t)c); kprint("\n");

    kfree(b);
    char *d = kmalloc(150);
    kprint("HEAP: freed b, d = "); kprint_hex((uint64_t)d); kprint("\n");

    if (d == b)
        kprint("HEAP: d got b's spot!\n");

    char *p[10];
    for (int i = 0; i < 10; i++)
        p[i] = kmalloc(1000);
    for (int i = 0; i < 10; i++)
        kfree(p[i]);

    char *big = kmalloc(9000);
    kprint("HEAP: p[0] = "); kprint_hex((uint64_t)p[0]);
    kprint(" big = "); kprint_hex((uint64_t)big); kprint("\n");
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
    kprint(same ? "HEAP: krealloc kept the data\n" : "HEAP: krealloc LOST the data\n");

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
    kprint("HEAP: mapped "); kprint_dec(st.mapped);
    kprint(" used "); kprint_dec(st.used);
    kprint(" free "); kprint_dec(st.free);
    kprint(" blocks "); kprint_dec(st.blocks); kprint("\n");
    kprint(heap_check() ? "HEAP: heap_check OK\n" : "HEAP: heap_check FAILED\n");

    kprint("HEAP: stress test 20000 rounds...\n");
    uint64_t pmm_before = pmm_get_free_pages();
    uint64_t t0 = timer_get_ticks();
    bool stress_ok = heap_stress(20000);
    uint64_t t1 = timer_get_ticks();
    kprint(stress_ok ? "HEAP: stress test PASSED" : "HEAP: stress test FAILED");
    kprint(" in "); kprint_dec((t1 - t0) * 10); kprint(" ms\n");

    heap_get_stats(&st);
    kprint("HEAP: mapped "); kprint_dec(st.mapped);
    kprint(" used "); kprint_dec(st.used);
    kprint(" free "); kprint_dec(st.free);
    kprint(" blocks "); kprint_dec(st.blocks); kprint("\n");
    kprint("HEAP: pmm free pages before "); kprint_dec(pmm_before);
    kprint(" after "); kprint_dec(pmm_get_free_pages()); kprint("\n");

    uint8_t previous = 0;

    for (;;) {
        uint8_t current = keyboard_get_last_scancode();

        if (current != previous) {
            previous = current;

            if (current == 0x31) {
                kprint("N pressed (shout out to narcito)\n");
            }

            if (current == 0xB1) {
                kprint("N released\n");
            }
        }

        __asm__ volatile ("hlt");
    }
}

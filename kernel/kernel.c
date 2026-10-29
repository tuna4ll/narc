#include <stdbool.h>
#include <stddef.h>
#include <limine.h>
#include "keyboard.h"
#include "io.h"
#include "cpu.h"
#include "terminal.h"
#include "pit.h"
#include "idt.h"
#include "pic.h"
 
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

#include "pic.h"

void kmain(void) {
    outb(0xE9, 'K');

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)
        || framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1) {
        outb(0xE9, 'E');
        khalt();
    }

    terminal_init(framebuffer_request.response->framebuffers[0]);

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

    pic_clear_mask(0); // PIT
    pic_clear_mask(1); // Keyboard

    pit_init(100);
    kprint("Testing INT3...\n");

    __asm__ volatile ("int3");
    kprint("Interrupt returned!\n");

    kprint("Enabling interrupts...\n");
    __asm__ volatile ("sti");

    uint8_t previous = 0;

    for (;;) {
        uint8_t current = keyboard_get_last_scancode();

        if (current != previous) {
            previous = current;

            if (current == 0x31) {
                kprint("N pressed (shout out to narcito)\n");
            }

            if (current == 0xB1) {
                kprint("N releasd\n");
            }
        }

        __asm__ volatile ("hlt");
    }

}
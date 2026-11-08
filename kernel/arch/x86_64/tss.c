#include <stdint.h>

#include "tss.h"
#include "gdt.h"
#include "kprint.h"

static struct tss tss;

static uint8_t kernel_stack[16384] __attribute__((aligned(16)));
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));

void tss_init(void) {
    tss.rsp0 = (uint64_t)kernel_stack + sizeof(kernel_stack);
    tss.ist1 = (uint64_t)double_fault_stack + sizeof(double_fault_stack);
    tss.iopb = sizeof(tss);

    gdt_set_tss((uint64_t)&tss, sizeof(tss) - 1);

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)TSS_SEG));

    kprint("TSS initialized!\n");
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

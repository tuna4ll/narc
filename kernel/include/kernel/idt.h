#pragma once
#include <kernel/arch.h>

#define IDT_VECTORS 48

typedef void (*interrupt_handler_t)(struct task_frame *frame);

void idt_init(void);
void idt_set_handler(unsigned vector, interrupt_handler_t handler);

#pragma once
#include <stdint.h>
void gdt_init(uint64_t rsp0);
void gdt_set_kernel_stack(uint64_t rsp0);

#pragma once
#include <stdint.h>

#define NARC_RELEASE "0.2.0"

long system_uname(uint64_t address);
long system_sethostname(uint64_t address, uint64_t length);

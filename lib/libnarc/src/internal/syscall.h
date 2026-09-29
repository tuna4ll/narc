#pragma once

#include <narcos/narc.h>

narc_result_t __narc_call(uint64_t id, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5,
                          uint64_t a6);

static inline narc_result_t __narc_call3(uint64_t id, uint64_t a1,
                                         uint64_t a2, uint64_t a3) {
    return __narc_call(id, a1, a2, a3, 0, 0, 0);
}

static inline narc_result_t __narc_call4(uint64_t id, uint64_t a1,
                                         uint64_t a2, uint64_t a3,
                                         uint64_t a4) {
    return __narc_call(id, a1, a2, a3, a4, 0, 0);
}

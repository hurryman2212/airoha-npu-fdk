/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_ARCH_RISCV_CACHE_H
#define NPU_ARCH_RISCV_CACHE_H

#include "an7581/platform/types.h"

/*
 * Discard the L1 D-cache line containing address without writeback.
 *
 * This SiFive custom instruction loses dirty data.  Callers must prove that
 * device ownership has already transferred and must not use it as a general
 * cache invalidate primitive.
 */
void an7581_l1_dcache_discard(const void *address);

#endif

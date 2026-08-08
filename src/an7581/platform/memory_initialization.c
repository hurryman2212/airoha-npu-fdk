/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/memory_initialization.h"

#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/runtime/memory.h"

void an7581_shared_sram_reset(void) {
  (void)npu_memset((void *)(uintptr_t)AN7581_NPU_SHARED_SRAM_BASE, 0U,
                   AN7581_NPU_SHARED_SRAM_SIZE);
  an7581_memory_barrier();
}

void an7581_l2_cache_initialize(void) {
  an7581_mmio_write32(AN7581_NPU_L2_CACHE_CONTROL, AN7581_NPU_L2_CACHE_ENABLE);
  an7581_memory_barrier();
  (void)npu_memset((void *)(uintptr_t)AN7581_NPU_L2_WORKSPACE_BASE, 0U,
                   AN7581_NPU_L2_WORKSPACE_SIZE);
  an7581_memory_barrier();
}

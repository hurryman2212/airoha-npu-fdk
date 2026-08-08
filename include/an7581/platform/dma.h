/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_DMA_H
#define AN7581_DMA_H

#include "an7581/platform/types.h"

#define AN7581_DMA_PHYSICAL_MASK UINT32_C(0x3fffffff)
#define AN7581_DMA_LOCAL_ALIAS_BASE UINT32_C(0x40000000)
#define AN7581_DMA_WINDOW_SIZE UINT32_C(0x40000000)

static inline uint32_t an7581_dma_local_alias(uint32_t dma_address) {
  return (dma_address & AN7581_DMA_PHYSICAL_MASK) | AN7581_DMA_LOCAL_ALIAS_BASE;
}

static inline void an7581_dma_memory_barrier(void) {
#ifdef AN7581_MMIO_TEST
  __asm__ volatile("" ::: "memory");
#else
  __asm__ volatile("fence iorw, iorw" ::: "memory");
#endif
}

bool an7581_dma_buffer_map(uint32_t dma_address, uint32_t length,
                           uint32_t alignment, uint32_t *local_address);

#endif

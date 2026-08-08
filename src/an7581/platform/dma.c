/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/dma.h"

static bool alignment_is_power_of_two(uint32_t alignment) {
  return alignment != 0U && (alignment & (alignment - 1U)) == 0U;
}

bool an7581_dma_buffer_map(uint32_t dma_address, uint32_t length,
                           uint32_t alignment, uint32_t *local_address) {
  uint32_t physical_offset;

  if (length == 0U || local_address == NULL ||
      !alignment_is_power_of_two(alignment) ||
      (dma_address & (alignment - 1U)) != 0U)
    return false;

  physical_offset = dma_address & AN7581_DMA_PHYSICAL_MASK;
  if (length > AN7581_DMA_WINDOW_SIZE - physical_offset)
    return false;

  *local_address = an7581_dma_local_alias(dma_address);
  return true;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/vdma.h"

#include "an7581/platform/mmio.h"

enum npu_runtime_result an7581_vdma_copy(uint32_t channel,
                                         uint32_t source_address,
                                         uint32_t destination_address,
                                         uint32_t length, uint32_t poll_limit) {
  uint32_t channel_base;
  uint32_t completion_bit;
  uint32_t poll;

  if (channel >= AN7581_VDMA_CHANNEL_COUNT || source_address == 0U ||
      destination_address == 0U || length == 0U ||
      length > AN7581_VDMA_TRANSFER_LENGTH_LIMIT || poll_limit == 0U ||
      (source_address & (sizeof(uint32_t) - 1U)) != 0U ||
      (destination_address & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  channel_base =
      AN7581_VDMA_CHANNEL_BASE + channel * AN7581_VDMA_CHANNEL_STRIDE;
  completion_bit = UINT32_C(1) << channel;
  an7581_mmio_write32(channel_base, source_address);
  an7581_mmio_write32(channel_base + sizeof(uint32_t), destination_address);
  an7581_mmio_write32(channel_base + 2U * sizeof(uint32_t),
                      (length << 16U) | AN7581_VDMA_TRANSFER_CONTROL);

  for (poll = 0U; poll < poll_limit; ++poll) {
    if ((an7581_mmio_read32(AN7581_VDMA_COMPLETION_STATUS) & completion_bit) !=
        0U) {
      an7581_mmio_write32(AN7581_VDMA_COMPLETION_STATUS, completion_bit);
      return NPU_RUNTIME_SUCCESS;
    }
  }
  return NPU_RUNTIME_TIMEOUT;
}

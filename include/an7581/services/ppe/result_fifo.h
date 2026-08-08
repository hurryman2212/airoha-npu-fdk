/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_PPE_RESULT_FIFO_H
#define NPU_PPE_RESULT_FIFO_H

#include "an7581/platform/types.h"

#define NPU_PPE_RESULT_FIFO_BATCH_LIMIT UINT32_C(0x00000100)
#define NPU_PPE_RESULT_FIFO_ACK UINT32_C(0x40000000)

struct npu_ppe_result_fifo_operations {
  uint32_t (*read_count)(void *context);
  uint32_t (*read_status)(void *context);
  uint32_t (*read_metadata)(void *context);
  bool (*dispatch_packet)(void *context, uint16_t buffer_id,
                          uint16_t flow_entry, uint8_t route);
  void (*release_buffer)(void *context, uint16_t buffer_id);
  void (*acknowledge)(void *context, uint32_t value);
};

struct npu_ppe_result_fifo_result {
  uint32_t available;
  uint32_t processed;
  uint32_t release_only;
  uint32_t dispatched;
  uint32_t dispatch_failures;
  bool stopped_on_invalid_status;
};

bool npu_ppe_result_fifo_process(
    uint32_t budget, const struct npu_ppe_result_fifo_operations *operations,
    void *context, struct npu_ppe_result_fifo_result *result);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/ppe/result_fifo.h"

#include "an7581/runtime/memory.h"

#define NPU_PPE_RESULT_COUNT_MASK UINT32_C(0x0000ffff)
#define NPU_PPE_RESULT_BUFFER_ID_MASK UINT32_C(0x0000ffff)
#define NPU_PPE_RESULT_RELEASE_ONLY (UINT32_C(1) << 16)
#define NPU_PPE_RESULT_INVALID (UINT32_C(1) << 31)
#define NPU_PPE_RESULT_FLOW_MASK UINT32_C(0x0000ffff)
#define NPU_PPE_RESULT_ROUTE_SHIFT 16U
#define NPU_PPE_RESULT_ROUTE_MASK UINT32_C(0x0000001f)

static bool
operations_are_valid(const struct npu_ppe_result_fifo_operations *operations) {
  return operations != NULL && operations->read_count != NULL &&
         operations->read_status != NULL && operations->read_metadata != NULL &&
         operations->dispatch_packet != NULL &&
         operations->release_buffer != NULL && operations->acknowledge != NULL;
}

bool npu_ppe_result_fifo_process(
    uint32_t budget, const struct npu_ppe_result_fifo_operations *operations,
    void *context, struct npu_ppe_result_fifo_result *result) {
  uint32_t limit;

  if (budget == 0U || !operations_are_valid(operations) || result == NULL)
    return false;

  (void)npu_memset(result, 0U, sizeof(*result));
  result->available =
      operations->read_count(context) & NPU_PPE_RESULT_COUNT_MASK;
  limit = result->available;
  if (limit > budget)
    limit = budget;
  if (limit > NPU_PPE_RESULT_FIFO_BATCH_LIMIT)
    limit = NPU_PPE_RESULT_FIFO_BATCH_LIMIT;

  while (result->processed < limit) {
    uint32_t status = operations->read_status(context);
    uint32_t metadata;
    uint16_t buffer_id;

    if ((status & NPU_PPE_RESULT_INVALID) != 0U) {
      result->stopped_on_invalid_status = true;
      break;
    }

    metadata = operations->read_metadata(context);
    buffer_id = (uint16_t)(status & NPU_PPE_RESULT_BUFFER_ID_MASK);
    if ((status & NPU_PPE_RESULT_RELEASE_ONLY) != 0U) {
      operations->release_buffer(context, buffer_id);
      ++result->release_only;
    } else {
      uint16_t flow_entry = (uint16_t)(metadata & NPU_PPE_RESULT_FLOW_MASK);
      uint8_t route = (uint8_t)((metadata >> NPU_PPE_RESULT_ROUTE_SHIFT) &
                                NPU_PPE_RESULT_ROUTE_MASK);

      if (operations->dispatch_packet(context, buffer_id, flow_entry, route)) {
        ++result->dispatched;
      } else {
        operations->release_buffer(context, buffer_id);
        ++result->dispatch_failures;
      }
    }

    operations->acknowledge(context, NPU_PPE_RESULT_FIFO_ACK);
    ++result->processed;
  }

  return true;
}

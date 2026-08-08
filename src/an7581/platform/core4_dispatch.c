/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/core4_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

enum npu_runtime_result
an7581_core4_dispatch_step(struct an7581_core4_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core4_dispatch_result *result) {
  struct an7581_core4_worker_result worker_result;
  an7581_core4_worker_step worker;
  void *worker_context;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_CORE4_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }
  if (!dispatch->initialized) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  result->role = AN7581_CORE4_DISPATCH_ROLE_TX_DONE;
  worker = dispatch->worker;
  an7581_dma_memory_barrier();
  if (worker == NULL) {
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_worker = true;
    result->should_backoff = true;
    return result->status;
  }

  worker_context = dispatch->worker_context;
  an7581_dma_memory_barrier();
  (void)npu_memset(&worker_result, 0U, sizeof(worker_result));
  result->status = worker(worker_context, &worker_result);
  result->should_backoff = worker_result.should_backoff;
  return result->status;
}

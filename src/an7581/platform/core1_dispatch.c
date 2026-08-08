/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/core1_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

enum npu_runtime_result
an7581_core1_dispatch_initialize(struct an7581_core1_dispatch *dispatch) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (dispatch->initialized || dispatch->worker != NULL ||
      dispatch->worker_context != NULL || dispatch->quiesce_requested ||
      dispatch->quiesced)
    return NPU_RUNTIME_REJECTED;

  (void)npu_memset(dispatch, 0U, sizeof(*dispatch));
  dispatch->initialized = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core1_dispatch_publish(struct an7581_core1_dispatch *dispatch,
                              an7581_core1_worker_step worker,
                              void *worker_context) {
  if (dispatch == NULL || worker == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (dispatch->worker != NULL || dispatch->worker_context != NULL ||
      dispatch->quiesce_requested || dispatch->quiesced)
    return NPU_RUNTIME_REJECTED;

  dispatch->worker_context = worker_context;
  an7581_dma_memory_barrier();
  dispatch->worker = worker;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core1_dispatch_request_quiesce(struct an7581_core1_dispatch *dispatch,
                                      void *worker_context) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (dispatch->worker == NULL || dispatch->worker_context != worker_context)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (dispatch->quiesce_requested)
    return NPU_RUNTIME_SUCCESS;
  if (dispatch->quiesced)
    return NPU_RUNTIME_OUT_OF_RANGE;

  dispatch->quiesced = false;
  an7581_dma_memory_barrier();
  dispatch->quiesce_requested = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core1_dispatch_unpublish(struct an7581_core1_dispatch *dispatch,
                                void *worker_context) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!dispatch->quiesce_requested || !dispatch->quiesced)
    return NPU_RUNTIME_EMPTY;
  if (dispatch->worker == NULL || dispatch->worker_context != worker_context)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->worker = NULL;
  an7581_dma_memory_barrier();
  dispatch->worker_context = NULL;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core1_dispatch_resume(struct an7581_core1_dispatch *dispatch) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!dispatch->quiesce_requested || !dispatch->quiesced)
    return NPU_RUNTIME_REJECTED;
  if (dispatch->worker != NULL || dispatch->worker_context != NULL)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->quiesce_requested = false;
  an7581_dma_memory_barrier();
  dispatch->quiesced = false;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core1_dispatch_step(struct an7581_core1_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core1_dispatch_result *result) {
  struct an7581_core1_worker_result worker_result;
  an7581_core1_worker_step worker;
  void *worker_context;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_CORE1_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }
  if (!dispatch->initialized) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  result->role = AN7581_CORE1_DISPATCH_ROLE_RX_REFILL;
  result->quiesce_requested = dispatch->quiesce_requested;
  an7581_dma_memory_barrier();
  if (result->quiesce_requested) {
    dispatch->quiesced = true;
    an7581_dma_memory_barrier();
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_worker = true;
    result->quiesced = true;
    result->should_backoff = true;
    return result->status;
  }
  if (dispatch->quiesced) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

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

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/core2_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool
dispatch_is_uninitialized(const struct an7581_core2_dispatch *dispatch) {
  return !dispatch->initialized && dispatch->tr471 == NULL &&
         dispatch->worker == NULL && dispatch->worker_context == NULL &&
         dispatch->wifi_tx_fast_path == NULL &&
         !dispatch->wifi_tx_fast_path_published &&
         !dispatch->quiesce_requested && !dispatch->quiesced;
}

enum npu_runtime_result
an7581_core2_dispatch_initialize(struct an7581_core2_dispatch *dispatch,
                                 struct an7581_tr471_runtime_dispatch *tr471) {
  if (dispatch == NULL || tr471 == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch_is_uninitialized(dispatch))
    return NPU_RUNTIME_REJECTED;

  dispatch->tr471 = tr471;
  an7581_dma_memory_barrier();
  dispatch->initialized = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
publish_worker(struct an7581_core2_dispatch *dispatch,
               an7581_core2_worker_step worker, void *worker_context,
               struct npu_wifi_tx_fast_path_runtime *wifi_tx_fast_path) {
  if (dispatch == NULL || worker == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized || dispatch->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (dispatch->wifi_tx_fast_path_published || dispatch->worker != NULL ||
      dispatch->worker_context != NULL || dispatch->wifi_tx_fast_path != NULL ||
      dispatch->quiesce_requested || dispatch->quiesced)
    return NPU_RUNTIME_REJECTED;

  dispatch->worker_context = worker_context;
  dispatch->wifi_tx_fast_path = wifi_tx_fast_path;
  an7581_dma_memory_barrier();
  dispatch->worker = worker;
  an7581_dma_memory_barrier();
  dispatch->wifi_tx_fast_path_published = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
wifi_tx_fast_path_step(void *context,
                       struct an7581_core2_worker_result *result) {
  struct npu_wifi_tx_fast_path_runtime *wifi_tx_fast_path = context;
  enum npu_runtime_result status;

  if (wifi_tx_fast_path == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!wifi_tx_fast_path->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = npu_wifi_tx_fast_path_runtime_step(wifi_tx_fast_path,
                                              &result->wifi_tx_fast_path);
  result->should_backoff = result->wifi_tx_fast_path.should_backoff;
  return status;
}

enum npu_runtime_result an7581_core2_dispatch_publish_wifi_tx_fast_path(
    struct an7581_core2_dispatch *dispatch,
    struct npu_wifi_tx_fast_path_runtime *wifi_tx_fast_path) {
  if (dispatch == NULL || wifi_tx_fast_path == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!wifi_tx_fast_path->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return publish_worker(dispatch, wifi_tx_fast_path_step, wifi_tx_fast_path,
                        wifi_tx_fast_path);
}

enum npu_runtime_result
an7581_core2_dispatch_request_quiesce(struct an7581_core2_dispatch *dispatch,
                                      void *worker_context) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized || dispatch->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!dispatch->wifi_tx_fast_path_published || dispatch->worker == NULL ||
      dispatch->worker_context != worker_context)
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
an7581_core2_dispatch_unpublish(struct an7581_core2_dispatch *dispatch,
                                void *worker_context) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized || dispatch->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!dispatch->quiesce_requested || !dispatch->quiesced)
    return NPU_RUNTIME_EMPTY;
  if (!dispatch->wifi_tx_fast_path_published || dispatch->worker == NULL ||
      dispatch->worker_context != worker_context)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->wifi_tx_fast_path_published = false;
  an7581_dma_memory_barrier();
  dispatch->worker = NULL;
  an7581_dma_memory_barrier();
  dispatch->worker_context = NULL;
  dispatch->wifi_tx_fast_path = NULL;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core2_dispatch_resume(struct an7581_core2_dispatch *dispatch) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized || dispatch->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!dispatch->quiesce_requested || !dispatch->quiesced)
    return NPU_RUNTIME_REJECTED;
  if (dispatch->wifi_tx_fast_path_published || dispatch->worker != NULL ||
      dispatch->worker_context != NULL || dispatch->wifi_tx_fast_path != NULL)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->quiesce_requested = false;
  an7581_dma_memory_barrier();
  dispatch->quiesced = false;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static bool status_is_failure(enum npu_runtime_result status) {
  return status != NPU_RUNTIME_SUCCESS && status != NPU_RUNTIME_EMPTY &&
         status != NPU_RUNTIME_FULL;
}

static enum npu_runtime_result
combined_status(const struct an7581_core2_dispatch_result *result) {
  if (status_is_failure(result->tr471.status))
    return result->tr471.status;
  if (result->wifi_tx_fast_path_polled &&
      status_is_failure(result->wifi_tx_fast_path_status))
    return result->wifi_tx_fast_path_status;
  if (result->tr471.status == NPU_RUNTIME_SUCCESS ||
      (result->wifi_tx_fast_path_polled &&
       result->wifi_tx_fast_path_status == NPU_RUNTIME_SUCCESS))
    return NPU_RUNTIME_SUCCESS;
  if (result->wifi_tx_fast_path_polled &&
      result->wifi_tx_fast_path_status == NPU_RUNTIME_FULL)
    return NPU_RUNTIME_FULL;
  return NPU_RUNTIME_EMPTY;
}

enum npu_runtime_result
an7581_core2_dispatch_step(struct an7581_core2_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core2_dispatch_result *result) {
  struct an7581_core2_worker_result worker_result;
  an7581_core2_worker_step worker;
  void *worker_context;
  bool wifi_tx_fast_path_published;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_CORE2_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }
  if (!dispatch->initialized) {
    an7581_dma_memory_barrier();
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_worker = true;
    result->should_backoff = true;
    return result->status;
  }
  an7581_dma_memory_barrier();
  if (dispatch->tr471 == NULL) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  result->tr471_polled = true;
  (void)an7581_tr471_runtime_dispatch_step(dispatch->tr471, hart_id,
                                           &result->tr471);

  result->quiesce_requested = dispatch->quiesce_requested;
  an7581_dma_memory_barrier();
  if (result->quiesce_requested) {
    dispatch->quiesced = true;
    an7581_dma_memory_barrier();
    result->wifi_tx_fast_path_status = NPU_RUNTIME_EMPTY;
    result->waiting_for_worker = true;
    result->quiesced = true;
    result->status = combined_status(result);
    result->should_backoff = result->tr471.should_backoff;
    return result->status;
  }
  if (dispatch->quiesced) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  wifi_tx_fast_path_published = dispatch->wifi_tx_fast_path_published;
  an7581_dma_memory_barrier();
  if (wifi_tx_fast_path_published) {
    worker = dispatch->worker;
    an7581_dma_memory_barrier();
    if (worker == NULL) {
      result->wifi_tx_fast_path_status = NPU_RUNTIME_OUT_OF_RANGE;
      result->status = result->wifi_tx_fast_path_status;
      result->should_backoff = true;
      return result->status;
    }
    worker_context = dispatch->worker_context;
    an7581_dma_memory_barrier();
    (void)npu_memset(&worker_result, 0U, sizeof(worker_result));
    result->wifi_tx_fast_path_polled = true;
    result->wifi_tx_fast_path_status = worker(worker_context, &worker_result);
    result->wifi_tx_fast_path = worker_result.wifi_tx_fast_path;
  }

  result->waiting_for_worker =
      !wifi_tx_fast_path_published && (result->tr471.waiting_for_publication ||
                                       result->tr471.timer_worker_ready);
  result->status = combined_status(result);
  result->should_backoff =
      result->tr471.should_backoff &&
      (!result->wifi_tx_fast_path_polled || worker_result.should_backoff);
  return result->status;
}

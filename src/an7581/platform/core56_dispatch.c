/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/core56_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool
dispatch_is_unpublished(const struct an7581_core56_dispatch *dispatch) {
  return dispatch->runtime == NULL && !dispatch->quiesce_requested &&
         !dispatch->core5_quiesced && !dispatch->core6_quiesced;
}

enum npu_runtime_result
an7581_core56_dispatch_publish(struct an7581_core56_dispatch *dispatch,
                               struct npu_wifi_rro_runtime *runtime) {
  if (dispatch == NULL || runtime == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch_is_unpublished(dispatch))
    return NPU_RUNTIME_REJECTED;
  if (!npu_wifi_rro_runtime_is_configured(runtime))
    return NPU_RUNTIME_OUT_OF_RANGE;

  dispatch->runtime = runtime;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core56_dispatch_request_quiesce(struct an7581_core56_dispatch *dispatch,
                                       struct npu_wifi_rro_runtime *runtime) {
  if (dispatch == NULL || runtime == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (dispatch->runtime != runtime)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (dispatch->quiesce_requested)
    return NPU_RUNTIME_SUCCESS;
  if (dispatch->core5_quiesced || dispatch->core6_quiesced)
    return NPU_RUNTIME_OUT_OF_RANGE;

  dispatch->core5_quiesced = false;
  dispatch->core6_quiesced = false;
  an7581_dma_memory_barrier();
  dispatch->quiesce_requested = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core56_dispatch_unpublish(struct an7581_core56_dispatch *dispatch,
                                 struct npu_wifi_rro_runtime *runtime) {
  if (dispatch == NULL || runtime == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->quiesce_requested || !dispatch->core5_quiesced ||
      !dispatch->core6_quiesced)
    return NPU_RUNTIME_EMPTY;
  if (dispatch->runtime != runtime)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->runtime = NULL;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core56_dispatch_resume(struct an7581_core56_dispatch *dispatch) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->quiesce_requested || !dispatch->core5_quiesced ||
      !dispatch->core6_quiesced)
    return NPU_RUNTIME_REJECTED;
  if (dispatch->runtime != NULL)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->quiesce_requested = false;
  an7581_dma_memory_barrier();
  dispatch->core5_quiesced = false;
  dispatch->core6_quiesced = false;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_core56_dispatch_step(struct an7581_core56_dispatch *dispatch,
                            uint32_t hart_id,
                            struct an7581_core56_dispatch_result *result) {
  struct npu_wifi_rro_runtime *runtime;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_CORE5_HART && hart_id != AN7581_CORE6_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }

  result->quiesce_requested = dispatch->quiesce_requested;
  an7581_dma_memory_barrier();
  if (result->quiesce_requested) {
    if (hart_id == AN7581_CORE5_HART)
      dispatch->core5_quiesced = true;
    else
      dispatch->core6_quiesced = true;
    an7581_dma_memory_barrier();
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_runtime = true;
    result->quiesced = true;
    result->should_backoff = true;
    return result->status;
  }
  if (dispatch->core5_quiesced || dispatch->core6_quiesced) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  runtime = dispatch->runtime;
  an7581_dma_memory_barrier();
  if (runtime == NULL) {
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_runtime = true;
    result->should_backoff = true;
    return result->status;
  }
  if (!npu_wifi_rro_runtime_is_configured(runtime)) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  if (hart_id == AN7581_CORE5_HART) {
    result->role = AN7581_CORE56_DISPATCH_ROLE_CPU_QUEUE;
    result->status =
        npu_wifi_rro_runtime_step_cpu_queue(runtime, &result->service);
  } else {
    result->role = AN7581_CORE56_DISPATCH_ROLE_INDICATION;
    result->status =
        npu_wifi_rro_runtime_step_indication(runtime, &result->service);
  }
  result->should_backoff = result->service.should_backoff;
  return result->status;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/core7_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool
dispatch_is_uninitialized(const struct an7581_core7_dispatch *dispatch) {
  return !dispatch->initialized && dispatch->tr471 == NULL &&
         dispatch->tunnel == NULL && !dispatch->tunnel_published;
}

enum npu_runtime_result
an7581_core7_dispatch_initialize(struct an7581_core7_dispatch *dispatch,
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

enum npu_runtime_result
an7581_core7_dispatch_publish_tunnel(struct an7581_core7_dispatch *dispatch,
                                     struct an7581_tunnel_platform *tunnel) {
  if (dispatch == NULL || tunnel == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->initialized || dispatch->tr471 == NULL || !tunnel->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (dispatch->tunnel_published || dispatch->tunnel != NULL)
    return NPU_RUNTIME_REJECTED;

  dispatch->tunnel = tunnel;
  an7581_dma_memory_barrier();
  dispatch->tunnel_published = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static bool
tr471_is_running(const struct an7581_tr471_runtime_dispatch *dispatch) {
  const struct npu_tr471_runtime_io *runtime;
  bool published;

  published = dispatch->published;
  an7581_dma_memory_barrier();
  if (!published)
    return false;
  runtime = dispatch->runtime;
  an7581_dma_memory_barrier();
  return runtime != NULL && runtime->state != NULL && runtime->state->running;
}

static bool status_is_failure(enum npu_runtime_result status) {
  return status != NPU_RUNTIME_SUCCESS && status != NPU_RUNTIME_EMPTY;
}

static enum npu_runtime_result
combined_status(const struct an7581_core7_dispatch_result *result) {
  if (status_is_failure(result->tr471.status))
    return result->tr471.status;
  if (result->tunnel_polled && status_is_failure(result->tunnel_status))
    return result->tunnel_status;
  if (result->tr471.status == NPU_RUNTIME_SUCCESS ||
      (result->tunnel_polled && result->tunnel_status == NPU_RUNTIME_SUCCESS))
    return NPU_RUNTIME_SUCCESS;
  return NPU_RUNTIME_EMPTY;
}

enum npu_runtime_result
an7581_core7_dispatch_step(struct an7581_core7_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core7_dispatch_result *result) {
  struct an7581_tunnel_platform *tunnel;
  bool tunnel_published;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_CORE7_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }
  if (!dispatch->initialized) {
    an7581_dma_memory_barrier();
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_runtime = true;
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

  tunnel_published = dispatch->tunnel_published;
  an7581_dma_memory_barrier();
  if (tr471_is_running(dispatch->tr471)) {
    result->tunnel_suppressed = true;
  } else if (tunnel_published) {
    tunnel = dispatch->tunnel;
    an7581_dma_memory_barrier();
    if (tunnel == NULL || !tunnel->initialized) {
      result->tunnel_status = NPU_RUNTIME_OUT_OF_RANGE;
      result->status = result->tunnel_status;
      result->should_backoff = true;
      return result->status;
    }
    result->tunnel_polled = true;
    result->tunnel_status = an7581_tunnel_platform_step(
        tunnel, AN7581_CORE7_TUNNEL_CHANNEL, &result->tunnel);
  }

  result->waiting_for_runtime =
      result->tr471.waiting_for_publication && !tunnel_published;
  result->status = combined_status(result);
  result->should_backoff =
      result->tr471.should_backoff &&
      (!result->tunnel_polled || result->tunnel_status != NPU_RUNTIME_SUCCESS);
  return result->status;
}

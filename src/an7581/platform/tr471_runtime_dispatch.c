/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tr471_runtime_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool runtime_is_ready(const struct npu_tr471_runtime_io *runtime) {
  return runtime != NULL && runtime->initialized && runtime->state != NULL &&
         runtime->tdma != NULL && runtime->tdma->initialized;
}

static bool timer_matches_runtime(const struct an7581_tr471_timer *timer,
                                  const struct npu_tr471_runtime_io *runtime) {
  return timer != NULL && timer->initialized && timer->tr471 == runtime->state;
}

static bool
dispatch_is_unpublished(const struct an7581_tr471_runtime_dispatch *dispatch) {
  return !dispatch->published && dispatch->runtime == NULL &&
         dispatch->timer == NULL && dispatch->wake_control == NULL &&
         dispatch->wake_context == NULL && !dispatch->control_notified &&
         !dispatch->timer_worker_ready;
}

enum npu_runtime_result an7581_tr471_runtime_dispatch_publish(
    struct an7581_tr471_runtime_dispatch *dispatch,
    const struct an7581_tr471_runtime_dispatch_config *config) {
  if (dispatch == NULL || config == NULL || config->wake_control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch_is_unpublished(dispatch))
    return NPU_RUNTIME_REJECTED;
  if (!runtime_is_ready(config->runtime) ||
      !timer_matches_runtime(config->timer, config->runtime) ||
      config->transmit_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT ||
      config->receive_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  dispatch->runtime = config->runtime;
  dispatch->timer = config->timer;
  dispatch->wake_control = config->wake_control;
  dispatch->wake_context = config->wake_context;
  dispatch->transmit_budget = config->transmit_budget;
  dispatch->receive_budget = config->receive_budget;
  an7581_dma_memory_barrier();
  dispatch->published = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

bool an7581_tr471_runtime_dispatch_timer_worker_ready(
    const struct an7581_tr471_runtime_dispatch *dispatch) {
  bool ready;

  if (dispatch == NULL)
    return false;
  ready = dispatch->published && dispatch->timer_worker_ready;
  an7581_dma_memory_barrier();
  return ready;
}

static enum npu_runtime_result
dispatch_timer_worker(struct an7581_tr471_runtime_dispatch *dispatch,
                      struct an7581_tr471_runtime_dispatch_result *result) {
  an7581_tr471_runtime_dispatch_wake wake_control;
  struct an7581_tr471_timer *timer = dispatch->timer;
  void *wake_context;
  enum npu_runtime_result status;

  an7581_dma_memory_barrier();
  if (!runtime_is_ready(dispatch->runtime) ||
      !timer_matches_runtime(timer, dispatch->runtime)) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  result->role = AN7581_TR471_RUNTIME_DISPATCH_ROLE_TIMER;
  if (!timer->interrupt_registered) {
    status = an7581_tr471_timer_interrupt_register(
        timer, AN7581_TR471_TIMER_HART, true);
    if (status != NPU_RUNTIME_SUCCESS) {
      result->status = status;
      result->should_backoff = true;
      return result->status;
    }
  }

  if (!dispatch->control_notified) {
    wake_control = dispatch->wake_control;
    wake_context = dispatch->wake_context;
    an7581_dma_memory_barrier();
    if (wake_control == NULL) {
      result->status = NPU_RUNTIME_OUT_OF_RANGE;
      result->should_backoff = true;
      return result->status;
    }
    status = wake_control(wake_context, AN7581_TR471_CONTROL_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS) {
      result->status = status;
      result->should_backoff = true;
      return result->status;
    }
    dispatch->control_notified = true;
    an7581_dma_memory_barrier();
  }

  dispatch->timer_worker_ready = true;
  an7581_dma_memory_barrier();
  result->timer_worker_ready = true;
  result->status = NPU_RUNTIME_SUCCESS;
  result->should_backoff = true;
  return result->status;
}

static enum npu_runtime_result
dispatch_runtime_worker(struct an7581_tr471_runtime_dispatch *dispatch,
                        struct an7581_tr471_runtime_dispatch_result *result) {
  struct npu_tr471_runtime_io *runtime = dispatch->runtime;
  struct an7581_tr471_timer *timer = dispatch->timer;

  an7581_dma_memory_barrier();
  if (!runtime_is_ready(runtime) || !timer_matches_runtime(timer, runtime)) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  result->role = AN7581_TR471_RUNTIME_DISPATCH_ROLE_RUNTIME;
  if (!timer->timer_started) {
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_timer = true;
    result->should_backoff = true;
    return result->status;
  }

  result->status = npu_tr471_runtime_io_step(
      runtime, runtime->state->periodic_counter, dispatch->transmit_budget,
      dispatch->receive_budget, &result->service);
  result->should_backoff = result->service.transmitted_packet_count == 0U &&
                           result->service.received_packet_count == 0U;
  return result->status;
}

enum npu_runtime_result an7581_tr471_runtime_dispatch_step(
    struct an7581_tr471_runtime_dispatch *dispatch, uint32_t hart_id,
    struct an7581_tr471_runtime_dispatch_result *result) {
  bool published;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_TR471_TIMER_HART &&
      hart_id != AN7581_TR471_RUNTIME_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }

  published = dispatch->published;
  an7581_dma_memory_barrier();
  if (!published) {
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_publication = true;
    result->should_backoff = true;
    return result->status;
  }

  if (hart_id == AN7581_TR471_TIMER_HART)
    return dispatch_timer_worker(dispatch, result);
  return dispatch_runtime_worker(dispatch, result);
}

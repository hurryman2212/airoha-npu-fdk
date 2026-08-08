/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tr471_runtime_lifecycle.h"

#include "an7581/runtime/memory.h"

static bool operations_are_valid(
    const struct an7581_tr471_runtime_lifecycle_operations *operations) {
  return operations != NULL && operations->step_service != NULL &&
         operations->initialize_timer != NULL &&
         operations->publish_workers != NULL &&
         operations->wake_harts != NULL &&
         operations->timer_worker_ready != NULL &&
         operations->start_timer != NULL;
}

enum npu_runtime_result an7581_tr471_runtime_lifecycle_initialize(
    struct an7581_tr471_runtime_lifecycle *lifecycle,
    const struct an7581_tr471_runtime_lifecycle_config *config) {
  if (lifecycle == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->activation_allowed &&
      (!operations_are_valid(config->operations) ||
       config->operation_context == NULL))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(lifecycle, 0U, sizeof(*lifecycle));
  lifecycle->operations = config->operations;
  lifecycle->operation_context = config->operation_context;
  lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_ACTIVATION_GATED;
  lifecycle->last_status = NPU_RUNTIME_REJECTED;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static void
lifecycle_result_update(const struct an7581_tr471_runtime_lifecycle *lifecycle,
                        struct an7581_tr471_runtime_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->service_active = lifecycle->service_active;
  result->timer_initialized = lifecycle->timer_initialized;
  result->workers_published = lifecycle->workers_published;
  result->timer_worker_woken = lifecycle->timer_worker_woken;
  result->timer_started = lifecycle->timer_started;
  result->runtime_worker_woken = lifecycle->runtime_worker_woken;
  result->active = lifecycle->state == AN7581_TR471_RUNTIME_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result lifecycle_retryable_failure(
    struct an7581_tr471_runtime_lifecycle *lifecycle,
    struct an7581_tr471_runtime_lifecycle_result *result,
    enum npu_runtime_result status) {
  lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

enum npu_runtime_result an7581_tr471_runtime_lifecycle_step(
    struct an7581_tr471_runtime_lifecycle *lifecycle,
    struct an7581_tr471_runtime_lifecycle_result *result) {
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  if (!lifecycle->activation_allowed) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!operations_are_valid(lifecycle->operations) ||
      lifecycle->operation_context == NULL)
    return lifecycle_retryable_failure(lifecycle, result,
                                       NPU_RUNTIME_OUT_OF_RANGE);

  if (!lifecycle->service_active) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_STARTING_SERVICE;
    ++lifecycle->service_attempt_count;
    status = lifecycle->operations->step_service(lifecycle->operation_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->service_active = true;
  }

  if (!lifecycle->timer_initialized) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_INITIALIZING_TIMER;
    ++lifecycle->timer_initialize_attempt_count;
    status =
        lifecycle->operations->initialize_timer(lifecycle->operation_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->timer_initialized = true;
  }

  if (!lifecycle->workers_published) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_PUBLISHING_WORKERS;
    ++lifecycle->publication_attempt_count;
    status =
        lifecycle->operations->publish_workers(lifecycle->operation_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->workers_published = true;
  }

  if (!lifecycle->timer_worker_woken) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_WAKING_TIMER_WORKER;
    ++lifecycle->timer_wake_attempt_count;
    status = lifecycle->operations->wake_harts(lifecycle->operation_context,
                                               AN7581_TR471_TIMER_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->timer_worker_woken = true;
  }

  if (!lifecycle->operations->timer_worker_ready(
          lifecycle->operation_context)) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_WAITING_FOR_TIMER_WORKER;
    lifecycle->last_status = NPU_RUNTIME_EMPTY;
    ++lifecycle->timer_wait_count;
    result->waiting_for_timer_worker = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  if (!lifecycle->timer_started) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_STARTING_TIMER;
    ++lifecycle->timer_start_attempt_count;
    status = lifecycle->operations->start_timer(lifecycle->operation_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->timer_started = true;
  }

  if (!lifecycle->runtime_worker_woken) {
    lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_WAKING_RUNTIME_WORKER;
    ++lifecycle->runtime_wake_attempt_count;
    status = lifecycle->operations->wake_harts(lifecycle->operation_context,
                                               AN7581_TR471_RUNTIME_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->runtime_worker_woken = true;
  }

  lifecycle->state = AN7581_TR471_RUNTIME_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_tr471_runtime_platform_initialize(
    struct an7581_tr471_runtime_platform *platform,
    const struct an7581_tr471_runtime_platform_config *config) {
  struct an7581_tr471_runtime_platform candidate = {0};

  if (platform == NULL || config == NULL || config->service_lifecycle == NULL ||
      config->state == NULL || config->runtime == NULL ||
      config->dispatch == NULL || config->wake_harts == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!config->service_lifecycle->initialized ||
      config->service_lifecycle->tr471 != config->state ||
      config->timer_clock_mhz == 0U ||
      config->transmit_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT ||
      config->receive_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  candidate.service_lifecycle = config->service_lifecycle;
  candidate.state = config->state;
  candidate.runtime = config->runtime;
  candidate.dispatch = config->dispatch;
  candidate.wake_harts = config->wake_harts;
  candidate.wake_context = config->wake_context;
  candidate.timer_clock_mhz = config->timer_clock_mhz;
  candidate.transmit_budget = config->transmit_budget;
  candidate.receive_budget = config->receive_budget;
  candidate.initialized = true;
  *platform = candidate;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result platform_step_service(void *context) {
  struct an7581_tr471_runtime_platform *platform = context;
  struct an7581_tr471_lifecycle_result result;
  enum npu_runtime_result status;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->service_lifecycle == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = an7581_tr471_lifecycle_step(platform->service_lifecycle, &result);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return result.active ? NPU_RUNTIME_SUCCESS : NPU_RUNTIME_OUT_OF_RANGE;
}

static enum npu_runtime_result platform_initialize_timer(void *context) {
  struct an7581_tr471_runtime_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return an7581_tr471_timer_initialize(&platform->timer, platform->state);
}

static enum npu_runtime_result platform_publish_workers(void *context) {
  struct an7581_tr471_runtime_platform *platform = context;
  struct an7581_tr471_runtime_dispatch_config config;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  config = (struct an7581_tr471_runtime_dispatch_config){
      .runtime = platform->runtime,
      .timer = &platform->timer,
      .wake_control = platform->wake_harts,
      .wake_context = platform->wake_context,
      .transmit_budget = platform->transmit_budget,
      .receive_budget = platform->receive_budget,
  };
  return an7581_tr471_runtime_dispatch_publish(platform->dispatch, &config);
}

static enum npu_runtime_result platform_wake_harts(void *context,
                                                   uint32_t hart_mask) {
  struct an7581_tr471_runtime_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->wake_harts == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return platform->wake_harts(platform->wake_context, hart_mask);
}

static bool platform_timer_worker_ready(void *context) {
  const struct an7581_tr471_runtime_platform *platform = context;

  return platform != NULL && platform->initialized &&
         an7581_tr471_runtime_dispatch_timer_worker_ready(platform->dispatch);
}

static enum npu_runtime_result platform_start_timer(void *context) {
  struct an7581_tr471_runtime_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return an7581_tr471_timer_start(&platform->timer,
                                  AN7581_TR471_TIMER_CONTROL_HART,
                                  platform->timer_clock_mhz, true);
}

static const struct an7581_tr471_runtime_lifecycle_operations
    platform_operations = {
        .step_service = platform_step_service,
        .initialize_timer = platform_initialize_timer,
        .publish_workers = platform_publish_workers,
        .wake_harts = platform_wake_harts,
        .timer_worker_ready = platform_timer_worker_ready,
        .start_timer = platform_start_timer,
};

const struct an7581_tr471_runtime_lifecycle_operations *
an7581_tr471_runtime_platform_operations(void) {
  return &platform_operations;
}

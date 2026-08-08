/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tr471_lifecycle.h"

#include "an7581/runtime/memory.h"

static bool lifecycle_operations_are_valid(
    const struct an7581_tr471_lifecycle_operations *operations) {
  return operations != NULL && operations->initialize_tdma != NULL &&
         operations->initialize_runtime != NULL &&
         operations->reset_flow != NULL;
}

static bool selected_flow_is_valid(const struct npu_tr471_state *state) {
  if (state->selected_ip_version == NPU_TR471_IPV4)
    return state->ipv4.valid;
  if (state->selected_ip_version == NPU_TR471_IPV6)
    return state->ipv6.valid;
  return false;
}

static bool buffer_address_is_valid(const struct npu_tr471_state *state) {
  uint32_t physical_offset =
      state->buffer_address & NPU_TR471_TDMA_BUFFER_ADDRESS_MASK;

  return (state->buffer_address & (NPU_TR471_TDMA_PACKET_BUFFER_SIZE - 1U)) ==
             0U &&
         physical_offset <= (NPU_TR471_TDMA_BUFFER_ADDRESS_MASK + 1U) -
                                NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT;
}

enum npu_runtime_result an7581_tr471_configuration_readiness(
    const struct npu_tr471_state *state, uint32_t shared_buffer_extent,
    struct an7581_tr471_configuration_readiness *readiness) {
  if (state == NULL || readiness == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(readiness, 0U, sizeof(*readiness));
  if (!state->buffer_address_valid)
    readiness->missing |= AN7581_TR471_REQUIRED_BUFFER_ADDRESS;
  else if (!buffer_address_is_valid(state))
    readiness->invalid |= AN7581_TR471_REQUIRED_BUFFER_ADDRESS;

  if (shared_buffer_extent == 0U)
    readiness->missing |= AN7581_TR471_REQUIRED_BUFFER_EXTENT;
  else if (shared_buffer_extent < NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT)
    readiness->invalid |= AN7581_TR471_REQUIRED_BUFFER_EXTENT;

  if (!selected_flow_is_valid(state)) {
    if (state->selected_ip_version > NPU_TR471_IPV6)
      readiness->invalid |= AN7581_TR471_REQUIRED_FLOW;
    else
      readiness->missing |= AN7581_TR471_REQUIRED_FLOW;
  }

  if (readiness->invalid != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (readiness->missing != 0U)
    return NPU_RUNTIME_EMPTY;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_tr471_lifecycle_initialize(
    struct an7581_tr471_lifecycle *lifecycle,
    const struct an7581_tr471_lifecycle_config *config) {
  if (lifecycle == NULL || config == NULL || config->state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->activation_allowed &&
      !lifecycle_operations_are_valid(config->operations))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(lifecycle, 0U, sizeof(*lifecycle));
  lifecycle->tr471 = config->state;
  lifecycle->operations = config->operations;
  lifecycle->configuration_readiness = config->configuration_readiness;
  lifecycle->buffer_revision = config->buffer_revision;
  lifecycle->operation_context = config->operation_context;
  lifecycle->state = AN7581_TR471_LIFECYCLE_WAITING_FOR_CONFIGURATION;
  lifecycle->last_status = NPU_RUNTIME_EMPTY;
  lifecycle->shared_buffer_extent = config->shared_buffer_extent;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static void
lifecycle_result_update(const struct an7581_tr471_lifecycle *lifecycle,
                        struct an7581_tr471_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->tdma_initialized = lifecycle->tdma_initialized;
  result->runtime_initialized = lifecycle->runtime_initialized;
  result->active = lifecycle->state == AN7581_TR471_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result
lifecycle_retryable_failure(struct an7581_tr471_lifecycle *lifecycle,
                            struct an7581_tr471_lifecycle_result *result,
                            enum npu_runtime_result status) {
  lifecycle->state = AN7581_TR471_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

static enum npu_runtime_result
lifecycle_reconfigure_flow(struct an7581_tr471_lifecycle *lifecycle,
                           struct an7581_tr471_lifecycle_result *result) {
  enum npu_runtime_result status;

  if (lifecycle->observed_flow_revision == lifecycle->tr471->flow_revision)
    return NPU_RUNTIME_SUCCESS;

  lifecycle->state = AN7581_TR471_LIFECYCLE_RECONFIGURING_FLOW;
  ++lifecycle->flow_reset_attempt_count;
  status = lifecycle->operations->reset_flow(lifecycle->operation_context);
  if (status != NPU_RUNTIME_SUCCESS)
    return lifecycle_retryable_failure(lifecycle, result, status);

  lifecycle->observed_flow_revision = lifecycle->tr471->flow_revision;
  result->flow_reconfigured = true;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result lifecycle_configuration_readiness(
    const struct an7581_tr471_lifecycle *lifecycle,
    struct an7581_tr471_configuration_readiness *readiness) {
  if (lifecycle->configuration_readiness != NULL)
    return lifecycle->configuration_readiness(
        lifecycle->operation_context, lifecycle->tr471,
        lifecycle->shared_buffer_extent, readiness);
  return an7581_tr471_configuration_readiness(
      lifecycle->tr471, lifecycle->shared_buffer_extent, readiness);
}

static uint32_t
lifecycle_buffer_revision(const struct an7581_tr471_lifecycle *lifecycle) {
  if (lifecycle->buffer_revision != NULL)
    return lifecycle->buffer_revision(lifecycle->operation_context,
                                      lifecycle->tr471);
  return lifecycle->tr471->buffer_revision;
}

enum npu_runtime_result
an7581_tr471_lifecycle_step(struct an7581_tr471_lifecycle *lifecycle,
                            struct an7581_tr471_lifecycle_result *result) {
  enum npu_runtime_result status;
  uint32_t buffer_revision;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized || lifecycle->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  status = lifecycle_configuration_readiness(lifecycle, &result->readiness);
  if (status != NPU_RUNTIME_SUCCESS) {
    lifecycle->state = AN7581_TR471_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    lifecycle->last_status = status;
    ++lifecycle->configuration_wait_count;
    result->waiting_for_configuration = true;
    lifecycle_result_update(lifecycle, result);
    return status;
  }

  if (!lifecycle->activation_allowed) {
    lifecycle->state = AN7581_TR471_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!lifecycle_operations_are_valid(lifecycle->operations))
    return lifecycle_retryable_failure(lifecycle, result,
                                       NPU_RUNTIME_OUT_OF_RANGE);

  buffer_revision = lifecycle_buffer_revision(lifecycle);
  if (lifecycle->tdma_initialized &&
      lifecycle->observed_buffer_revision != buffer_revision) {
    lifecycle->state = AN7581_TR471_LIFECYCLE_CONFIGURATION_CONFLICT;
    lifecycle->last_status = NPU_RUNTIME_OWNERSHIP_ERROR;
    result->configuration_conflict = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  if (!lifecycle->tdma_initialized) {
    lifecycle->state = AN7581_TR471_LIFECYCLE_INITIALIZING_TDMA;
    ++lifecycle->tdma_attempt_count;
    status = lifecycle->operations->initialize_tdma(
        lifecycle->operation_context, lifecycle->tr471,
        lifecycle->shared_buffer_extent);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->tdma_initialized = true;
    lifecycle->observed_buffer_revision = buffer_revision;
    lifecycle->observed_flow_revision = lifecycle->tr471->flow_revision;
  }

  if (!lifecycle->runtime_initialized) {
    lifecycle->state = AN7581_TR471_LIFECYCLE_INITIALIZING_RUNTIME;
    ++lifecycle->runtime_attempt_count;
    status = lifecycle->operations->initialize_runtime(
        lifecycle->operation_context, lifecycle->tr471);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->runtime_initialized = true;
  }

  status = lifecycle_reconfigure_flow(lifecycle, result);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  lifecycle->state = AN7581_TR471_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_tr471_lifecycle_platform_initialize(
    struct an7581_tr471_lifecycle_platform *platform) {
  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
platform_initialize_tdma(void *context, const struct npu_tr471_state *state,
                         uint32_t shared_buffer_extent) {
  struct an7581_tr471_lifecycle_platform *platform = context;
  struct an7581_tr471_tdma_memory memory;
  enum npu_runtime_result status;

  if (platform == NULL || state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status =
      an7581_tr471_tdma_memory_resolve(state, shared_buffer_extent, &memory);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return an7581_tr471_tdma_platform_initialize(&platform->tdma, &memory);
}

static enum npu_runtime_result
platform_initialize_runtime(void *context, struct npu_tr471_state *state) {
  struct an7581_tr471_lifecycle_platform *platform = context;

  if (platform == NULL || state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || !platform->tdma.initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return npu_tr471_runtime_io_initialize(&platform->runtime, state,
                                         &platform->tdma.tdma);
}

static enum npu_runtime_result platform_reset_flow(void *context) {
  struct an7581_tr471_lifecycle_platform *platform = context;
  enum npu_runtime_result status;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || !platform->tdma.initialized ||
      !platform->runtime.initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = npu_tr471_tdma_tx_reset(&platform->tdma.tdma);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_tr471_runtime_io_cancel_pending(&platform->runtime);
}

static const struct an7581_tr471_lifecycle_operations platform_operations = {
    .initialize_tdma = platform_initialize_tdma,
    .initialize_runtime = platform_initialize_runtime,
    .reset_flow = platform_reset_flow,
};

const struct an7581_tr471_lifecycle_operations *
an7581_tr471_lifecycle_platform_operations(void) {
  return &platform_operations;
}

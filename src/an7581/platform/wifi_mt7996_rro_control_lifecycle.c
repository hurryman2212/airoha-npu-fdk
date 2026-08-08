/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_rro_control_lifecycle.h"

#include "an7581/runtime/memory.h"

enum npu_runtime_result an7581_wifi_mt7996_rro_control_platform_initialize(
    struct an7581_wifi_mt7996_rro_control_platform *platform,
    const struct an7581_wifi_mt7996_rro_control_platform_config *config) {
  size_t index;

  if (platform == NULL || config == NULL || config->pipeline == NULL ||
      config->control_plane == NULL || config->dispatch == NULL ||
      config->wake_workers == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->control_plane->additional_backend_count >
      NPU_WIFI_MT7996_CONTROL_ADDITIONAL_BACKEND_LIMIT -
          NPU_WIFI_MT7996_RRO_BACKEND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (config->control_plane->additional_backend_count != 0U &&
      config->control_plane->additional_backends == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->pipeline_config = *config->pipeline;
  platform->control_plane_config = *config->control_plane;
  platform->external_backend_count =
      config->control_plane->additional_backend_count;
  for (index = 0U; index < platform->external_backend_count; ++index)
    platform->additional_backends[index] =
        config->control_plane->additional_backends[index];

  platform->pipeline_config.reset_packet_ids = npu_wifi_packet_id_pool_reset;
  platform->pipeline_config.packet_id_context =
      config->control_plane->shared_packet_pool != NULL
          ? config->control_plane->shared_packet_pool
          : &platform->control_plane.packet_pool;
  platform->pipeline_config.reset_buffer_ids = npu_wifi_buffer_id_map_reset;
  platform->pipeline_config.buffer_id_context =
      &platform->control_plane.msdu_page_id_pool;
  platform->control_plane_config.additional_backends =
      platform->additional_backends;
  platform->control_plane_config.additional_backend_count =
      platform->external_backend_count;
  platform->dispatch = config->dispatch;
  platform->wake_workers = config->wake_workers;
  platform->wake_context = config->wake_context;
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_rro_control_lifecycle_initialize(
    struct an7581_wifi_mt7996_rro_control_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_rro_control_lifecycle_config *config) {
  if (lifecycle == NULL || config == NULL || config->configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->activation_allowed &&
      (config->platform == NULL || !config->platform->initialized))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(lifecycle, 0U, sizeof(*lifecycle));
  lifecycle->configuration = config->configuration;
  lifecycle->platform = config->platform;
  lifecycle->state =
      AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
  lifecycle->last_status = NPU_RUNTIME_EMPTY;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static void lifecycle_result_update(
    const struct an7581_wifi_mt7996_rro_control_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rro_control_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->pipeline_initialized = lifecycle->pipeline_initialized;
  result->backends_bound = lifecycle->backends_bound;
  result->control_plane_initialized = lifecycle->control_plane_initialized;
  result->runtime_published = lifecycle->runtime_published;
  result->workers_woken = lifecycle->workers_woken;
  result->active =
      lifecycle->state == AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result lifecycle_retryable_failure(
    struct an7581_wifi_mt7996_rro_control_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rro_control_lifecycle_result *result,
    enum npu_runtime_result status) {
  lifecycle->state = AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

static bool pipeline_configuration_is_ready(
    const struct an7581_wifi_mt7996_rro_control_platform *platform) {
  const struct npu_wifi_mt7996_rro_pipeline_config *config;

  if (platform == NULL)
    return false;
  config = &platform->pipeline_config;
  return config->memory.metadata_records.memory != NULL &&
         config->memory.metadata_trailers.memory != NULL &&
         config->memory.packet_buffers.memory != NULL &&
         config->page_pool_base != 0U && config->packet_buffer_count != 0U;
}

enum npu_runtime_result an7581_wifi_mt7996_rro_control_lifecycle_step(
    struct an7581_wifi_mt7996_rro_control_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rro_control_lifecycle_result *result) {
  const struct npu_wifi_backend_binding *rro_backends;
  struct an7581_wifi_mt7996_rro_control_platform *platform;
  size_t rro_backend_count;
  size_t index;
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized || lifecycle->configuration == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  if (!lifecycle->activation_allowed) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  platform = lifecycle->platform;
  if (platform == NULL || !platform->initialized)
    return lifecycle_retryable_failure(lifecycle, result,
                                       NPU_RUNTIME_OUT_OF_RANGE);

  if (!lifecycle->control_plane_initialized) {
    if (platform->control_plane_config.shared_packet_pool != NULL &&
        !platform->control_plane_config.shared_packet_pool->initialized) {
      lifecycle->state =
          AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
      lifecycle->last_status = NPU_RUNTIME_EMPTY;
      ++lifecycle->configuration_wait_count;
      result->waiting_for_configuration = true;
      lifecycle_result_update(lifecycle, result);
      return lifecycle->last_status;
    }
    lifecycle->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_INITIALIZING_CONTROL_PLANE;
    ++lifecycle->control_plane_attempt_count;
    platform->control_plane_config.configuration = lifecycle->configuration;
    platform->control_plane_config.activation_allowed = true;
    status = npu_wifi_mt7996_control_plane_initialize(
        &platform->control_plane, &platform->control_plane_config);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->control_plane_initialized = true;
  }

  if (!lifecycle->pipeline_initialized &&
      !pipeline_configuration_is_ready(platform)) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    lifecycle->last_status = NPU_RUNTIME_EMPTY;
    ++lifecycle->configuration_wait_count;
    result->waiting_for_configuration = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  if (!lifecycle->pipeline_initialized) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_INITIALIZING_PIPELINE;
    ++lifecycle->pipeline_attempt_count;
    platform->pipeline_config.configuration = lifecycle->configuration;
    status = npu_wifi_mt7996_rro_pipeline_initialize(
        &platform->pipeline, &platform->pipeline_config);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->pipeline_initialized = true;
  }

  if (!lifecycle->backends_bound) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_BINDING_BACKENDS;
    ++lifecycle->backend_bind_attempt_count;
    status = npu_wifi_mt7996_rro_pipeline_get_backend_bindings(
        &platform->pipeline, &rro_backends, &rro_backend_count);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    if (rro_backends == NULL ||
        rro_backend_count != NPU_WIFI_MT7996_RRO_BACKEND_COUNT)
      return lifecycle_retryable_failure(lifecycle, result,
                                         NPU_RUNTIME_OUT_OF_RANGE);
    for (index = 0U; index < rro_backend_count; ++index) {
      if (rro_backends[index].operations == NULL ||
          rro_backends[index].context == NULL)
        return lifecycle_retryable_failure(lifecycle, result,
                                           NPU_RUNTIME_OUT_OF_RANGE);
    }
    status = npu_wifi_mt7996_control_plane_bind_backends(
        &platform->control_plane, rro_backends, rro_backend_count);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    platform->rro_backend_count = rro_backend_count;
    lifecycle->backends_bound = true;
  }

  if (!lifecycle->runtime_published) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_PUBLISHING_RUNTIME;
    ++lifecycle->publication_attempt_count;
    status = an7581_core56_dispatch_publish(platform->dispatch,
                                            &platform->pipeline.runtime);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->runtime_published = true;
  }

  if (!lifecycle->workers_woken) {
    lifecycle->state = AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_WAKING_WORKERS;
    ++lifecycle->wake_attempt_count;
    status =
        platform->wake_workers(platform->wake_context,
                               AN7581_WIFI_MT7996_RRO_CONTROL_WORKER_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->workers_woken = true;
  }

  lifecycle->state = AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

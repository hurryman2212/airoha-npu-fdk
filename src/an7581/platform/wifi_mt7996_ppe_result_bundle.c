/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_ppe_result_bundle.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static enum npu_runtime_result release_packet(void *context,
                                              uint16_t packet_id) {
  struct an7581_wifi_mt7996_ppe_result_bundle *bundle = context;

  if (bundle == NULL || bundle->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_packet_id_pool_release(bundle->packet_pool, packet_id);
}

static enum npu_runtime_result
enqueue_packet(void *context, int16_t packet_id, uint16_t total_length,
               uint16_t flow_value, uint16_t route, uint8_t band, uint8_t flags,
               uint16_t fragment_length) {
  struct an7581_wifi_mt7996_ppe_result_bundle *bundle = context;

  if (bundle == NULL || !bundle->packet_queue.initialized)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_mt7996_packet_queue_enqueue(
      &bundle->packet_queue.service, packet_id, total_length, flow_value, route,
      band, flags, fragment_length);
}

static bool memory_is_valid(
    const struct an7581_wifi_mt7996_ppe_result_bundle_memory *memory) {
  uint32_t packet_span;

  if (memory->packet_queue.entries == NULL || memory->packet_mapping == NULL ||
      memory->result_registers == NULL || memory->result_mode == NULL ||
      memory->result_format == NULL || memory->result_route == NULL ||
      memory->packet_count == 0U ||
      memory->packet_count > NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT ||
      ((uintptr_t)memory->packet_mapping & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)memory->result_registers & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)memory->result_mode & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)memory->result_format & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)memory->result_route & (sizeof(uint32_t) - 1U)) != 0U ||
      !an7581_wifi_mt7996_tdma_delivery_memory_is_valid(&memory->tdma_delivery))
    return false;

  packet_span =
      memory->packet_count * NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE;
  return memory->packet_mapping_size >= packet_span &&
         memory->packet_dma_base <=
             NPU_WIFI_MT7996_TDMA_ADDRESS_MASK - packet_span + 1U &&
         memory->packet_queue.entry_memory_size >=
             AN7581_WIFI_MT7996_PACKET_QUEUE_MEMORY_SIZE &&
         (memory->packet_queue.fragment_entries == NULL ||
          memory->packet_queue.fragment_entry_memory_size >=
              AN7581_WIFI_MT7996_FRAGMENT_QUEUE_MEMORY_SIZE);
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_bundle_memory_resolve(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_ppe_result_bundle_memory *memory) {
  struct an7581_wifi_mt7996_ppe_result_bundle_memory candidate;
  enum npu_runtime_result status;
  uint32_t packet_mapping_address;
  uint32_t packet_span = NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT *
                         NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE;

  if (configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!configuration->packet_buffer_address_valid ||
      !an7581_dma_buffer_map(configuration->packet_buffer_address, packet_span,
                             NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE,
                             &packet_mapping_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  status = an7581_wifi_mt7996_packet_queue_memory_resolve_band(
      1U, &candidate.packet_queue);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status =
      an7581_wifi_mt7996_tdma_delivery_memory_resolve(&candidate.tdma_delivery);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = an7581_wifi_mt7996_ppe_result_registers_resolve(
      &candidate.result_registers);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  candidate.packet_mapping =
      (volatile uint8_t *)(uintptr_t)packet_mapping_address;
  candidate.result_mode = (volatile uint32_t *)(uintptr_t)
      AN7581_WIFI_MT7996_PPE_RESULT_MODE_ADDRESS;
  candidate.result_format = (volatile uint32_t *)(uintptr_t)
      AN7581_WIFI_MT7996_PPE_RESULT_FORMAT_ADDRESS;
  candidate.result_route = (volatile uint32_t *)(uintptr_t)
      AN7581_WIFI_MT7996_PPE_RESULT_ROUTE_ADDRESS;
  candidate.packet_mapping_size = packet_span;
  candidate.packet_dma_base =
      configuration->packet_buffer_address & NPU_WIFI_MT7996_TDMA_ADDRESS_MASK;
  candidate.packet_count = NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_bundle_initialize(
    struct an7581_wifi_mt7996_ppe_result_bundle *bundle,
    const struct an7581_wifi_mt7996_ppe_result_bundle_config *config) {
  struct an7581_wifi_mt7996_packet_queue_config packet_queue_config;
  struct an7581_wifi_mt7996_ppe_result_config result_config;
  struct an7581_wifi_mt7996_tdma_delivery_config tdma_delivery_config;
  enum npu_runtime_result status;

  if (bundle == NULL || config == NULL || config->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (bundle->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!config->packet_pool->initialized || !memory_is_valid(&config->memory))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(bundle, 0U, sizeof(*bundle));
  bundle->packet_pool = config->packet_pool;
  packet_queue_config = (struct an7581_wifi_mt7996_packet_queue_config){
      .memory = config->memory.packet_queue,
      .diagnostic_counters =
          {
              .entries_enqueued = config->diagnostic_counters != NULL
                                      ? &config->diagnostic_counters
                                             ->packet_queue_entries_enqueued
                                      : NULL,
              .queue_full =
                  config->diagnostic_counters != NULL
                      ? &config->diagnostic_counters->packet_queue_full
                      : NULL,
              .fragment_entries_enqueued =
                  config->diagnostic_counters != NULL
                      ? &config->diagnostic_counters->fragment_entries_enqueued
                      : NULL,
          },
      .hart_id = config->hart_id,
      .producer = config->packet_queue_producer,
      .fragment_producer = config->fragment_queue_producer,
      .band = 1U,
  };
  status = an7581_wifi_mt7996_packet_queue_platform_initialize(
      &bundle->packet_queue, &packet_queue_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  tdma_delivery_config = (struct an7581_wifi_mt7996_tdma_delivery_config){
      .memory = config->memory.tdma_delivery,
      .packet_mapping = config->memory.packet_mapping,
      .enqueue = enqueue_packet,
      .release = release_packet,
      .packet_context = bundle,
      .diagnostic_counters = config->tdma_diagnostic_counters,
      .packet_mapping_size = config->memory.packet_mapping_size,
      .packet_dma_base = config->memory.packet_dma_base,
      .packet_count = config->memory.packet_count,
  };
  status = an7581_wifi_mt7996_tdma_delivery_platform_initialize(
      &bundle->tdma_delivery, &tdma_delivery_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  *config->memory.result_mode = AN7581_WIFI_MT7996_PPE_RESULT_MODE_VALUE;
  *config->memory.result_format = AN7581_WIFI_MT7996_PPE_RESULT_FORMAT_VALUE;
  *config->memory.result_route =
      (*config->memory.result_route &
       AN7581_WIFI_MT7996_PPE_RESULT_ROUTE_PRESERVE_MASK) |
      AN7581_WIFI_MT7996_PPE_RESULT_ROUTE_VALUE;
  an7581_dma_memory_barrier();
  result_config = (struct an7581_wifi_mt7996_ppe_result_config){
      .registers = config->memory.result_registers,
      .packet_control = &bundle->tdma_delivery.delivery.packet_control,
      .diagnostic_counters = config->packet_pool->diagnostic_counters,
  };
  status =
      an7581_wifi_mt7996_ppe_result_initialize(&bundle->result, &result_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  bundle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_bundle_set_active(
    struct an7581_wifi_mt7996_ppe_result_bundle *bundle, bool active) {
  if (bundle == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!bundle->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (active) {
    if (bundle->result.interrupt_registered)
      return NPU_RUNTIME_SUCCESS;
    return an7581_wifi_mt7996_ppe_result_interrupt_register(&bundle->result,
                                                            true);
  }
  if (!bundle->result.interrupt_registered)
    return NPU_RUNTIME_SUCCESS;
  return an7581_wifi_mt7996_ppe_result_interrupt_unregister(&bundle->result);
}

static void lifecycle_result_update(
    const struct an7581_wifi_mt7996_ppe_result_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_ppe_result_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->bundle_initialized = lifecycle->bundle.initialized;
  result->active =
      lifecycle->state == AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result lifecycle_retryable_failure(
    struct an7581_wifi_mt7996_ppe_result_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_ppe_result_lifecycle_result *result,
    enum npu_runtime_result status) {
  lifecycle->state = AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_lifecycle_initialize(
    struct an7581_wifi_mt7996_ppe_result_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_ppe_result_lifecycle_config *config) {
  if (lifecycle == NULL || config == NULL || config->configuration == NULL ||
      config->packet_pool == NULL || config->completion_lifecycle == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (lifecycle->initialized)
    return NPU_RUNTIME_REJECTED;

  (void)npu_memset(lifecycle, 0U, sizeof(*lifecycle));
  lifecycle->configuration = config->configuration;
  lifecycle->packet_pool = config->packet_pool;
  lifecycle->completion_lifecycle = config->completion_lifecycle;
  lifecycle->memory = config->memory;
  lifecycle->diagnostic_counters = config->diagnostic_counters;
  lifecycle->tdma_diagnostic_counters = config->tdma_diagnostic_counters;
  lifecycle->hart_id = config->hart_id;
  lifecycle->packet_queue_producer = config->packet_queue_producer;
  lifecycle->fragment_queue_producer = config->fragment_queue_producer;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->state =
      AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_CONFIGURATION;
  lifecycle->last_status = NPU_RUNTIME_EMPTY;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_lifecycle_step(
    struct an7581_wifi_mt7996_ppe_result_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_ppe_result_lifecycle_result *result) {
  struct an7581_wifi_mt7996_ppe_result_bundle_memory resolved_memory;
  struct an7581_wifi_mt7996_ppe_result_bundle_config bundle_config;
  const struct an7581_wifi_mt7996_ppe_result_bundle_memory *memory;
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized || lifecycle->configuration == NULL ||
      lifecycle->packet_pool == NULL || lifecycle->completion_lifecycle == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  if (!lifecycle->configuration->packet_buffer_address_valid) {
    lifecycle->state =
        AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    lifecycle->last_status = NPU_RUNTIME_EMPTY;
    ++lifecycle->configuration_wait_count;
    result->waiting_for_configuration = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!lifecycle->activation_allowed) {
    lifecycle->state = AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!lifecycle->completion_lifecycle->shared_state_initialized ||
      !lifecycle->packet_pool->initialized) {
    lifecycle->state =
        AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_COMPLETION;
    lifecycle->last_status = NPU_RUNTIME_EMPTY;
    ++lifecycle->completion_wait_count;
    result->waiting_for_completion = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  if (!lifecycle->bundle.initialized) {
    lifecycle->state = AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_INITIALIZING;
    ++lifecycle->initialization_attempt_count;
    memory = lifecycle->memory;
    if (memory == NULL) {
      status = an7581_wifi_mt7996_ppe_result_bundle_memory_resolve(
          lifecycle->configuration, &resolved_memory);
      if (status != NPU_RUNTIME_SUCCESS)
        return lifecycle_retryable_failure(lifecycle, result, status);
      memory = &resolved_memory;
    }
    bundle_config = (struct an7581_wifi_mt7996_ppe_result_bundle_config){
        .memory = *memory,
        .packet_pool = lifecycle->packet_pool,
        .diagnostic_counters = lifecycle->diagnostic_counters,
        .tdma_diagnostic_counters = lifecycle->tdma_diagnostic_counters,
        .hart_id = lifecycle->hart_id,
        .packet_queue_producer = lifecycle->packet_queue_producer,
        .fragment_queue_producer = lifecycle->fragment_queue_producer,
    };
    status = an7581_wifi_mt7996_ppe_result_bundle_initialize(&lifecycle->bundle,
                                                             &bundle_config);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
  }

  if (lifecycle->completion_lifecycle->state !=
      AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVE) {
    lifecycle->state =
        AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_COMPLETION;
    lifecycle->last_status = NPU_RUNTIME_EMPTY;
    ++lifecycle->completion_wait_count;
    result->waiting_for_completion = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  if (!lifecycle->bundle.result.interrupt_registered) {
    lifecycle->state = AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVATING;
    ++lifecycle->activation_attempt_count;
    status = an7581_wifi_mt7996_ppe_result_bundle_set_active(&lifecycle->bundle,
                                                             true);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
  }

  lifecycle->state = AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_completion_lifecycle.h"

#include "an7581/platform/wifi_mt7996_completion_runtime.h"
#include "an7581/runtime/memory.h"

static bool lifecycle_operations_are_valid(
    const struct an7581_wifi_mt7996_completion_lifecycle_operations
        *operations) {
  return operations != NULL && operations->initialize_shared_state != NULL &&
         operations->initialize_pipeline != NULL &&
         operations->publish_pipeline != NULL &&
         operations->wake_workers != NULL;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_configuration_readiness(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_completion_configuration_readiness *readiness) {
  const struct npu_wifi_interface_configuration *descriptor_interface;
  const struct npu_wifi_interface_configuration *register_interface;

  if (configuration == NULL || readiness == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(readiness, 0U, sizeof(*readiness));
  if (!configuration->packet_buffer_address_valid) {
    readiness->missing |= AN7581_WIFI_MT7996_COMPLETION_REQUIRED_PACKET_BUFFER;
  } else if (configuration->packet_buffer_address == 0U ||
             (configuration->packet_buffer_address &
              (NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE - 1U)) != 0U) {
    readiness->invalid |= AN7581_WIFI_MT7996_COMPLETION_REQUIRED_PACKET_BUFFER;
  }

  if (!configuration->token_id_size_valid) {
    readiness->missing |= AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TOKEN_ID_SIZE;
  } else if (configuration->token_id_size < 2U ||
             configuration->token_id_size >
                 NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT) {
    readiness->invalid |= AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TOKEN_ID_SIZE;
  }

  descriptor_interface =
      &configuration->interface[NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_INTERFACE];
  if ((descriptor_interface->valid_fields & NPU_WIFI_VALID_TX_DONE_RING_BASE) ==
      0U) {
    readiness->missing |= AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_RING;
  } else if (descriptor_interface->tx_done_ring_base == 0U ||
             (descriptor_interface->tx_done_ring_base &
              (sizeof(uint32_t) - 1U)) != 0U) {
    readiness->invalid |= AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_RING;
  }

  register_interface =
      &configuration->interface[NPU_WIFI_MT7996_TX_DONE_REGISTER_INTERFACE];
  if ((register_interface->valid_fields &
       (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT)) !=
      (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT)) {
    readiness->missing |=
        AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_REGISTERS;
  } else if (register_interface->pcie_address == 0U ||
             (register_interface->pcie_address & (sizeof(uint32_t) - 1U)) !=
                 0U ||
             register_interface->descriptor_count == 0U ||
             register_interface->descriptor_count >
                 NPU_WIFI_MT7996_TX_DONE_RING_LIMIT) {
    readiness->invalid |=
        AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_REGISTERS;
  }

  if (readiness->invalid != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (readiness->missing != 0U)
    return NPU_RUNTIME_EMPTY;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_lifecycle_initialize(
    struct an7581_wifi_mt7996_completion_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_completion_lifecycle_config *config) {
  if (lifecycle == NULL || config == NULL || config->configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->activation_allowed &&
      !lifecycle_operations_are_valid(config->operations))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(lifecycle, 0U, sizeof(*lifecycle));
  lifecycle->configuration = config->configuration;
  lifecycle->operations = config->operations;
  lifecycle->operation_context = config->operation_context;
  lifecycle->state =
      AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_WAITING_FOR_CONFIGURATION;
  lifecycle->last_status = NPU_RUNTIME_EMPTY;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static void lifecycle_result_update(
    const struct an7581_wifi_mt7996_completion_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_completion_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->shared_state_initialized = lifecycle->shared_state_initialized;
  result->pipeline_initialized = lifecycle->pipeline_initialized;
  result->pipeline_published = lifecycle->pipeline_published;
  result->workers_woken = lifecycle->workers_woken;
  result->active =
      lifecycle->state == AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result lifecycle_retryable_failure(
    struct an7581_wifi_mt7996_completion_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_completion_lifecycle_result *result,
    enum npu_runtime_result status) {
  lifecycle->state = AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_lifecycle_step(
    struct an7581_wifi_mt7996_completion_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_completion_lifecycle_result *result) {
  enum npu_runtime_result shared_state_status;
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized || lifecycle->configuration == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  status = an7581_wifi_mt7996_completion_configuration_readiness(
      lifecycle->configuration, &result->readiness);
  if (!lifecycle->shared_state_initialized && lifecycle->activation_allowed &&
      lifecycle_operations_are_valid(lifecycle->operations) &&
      lifecycle->configuration->token_id_size_valid &&
      lifecycle->configuration->token_id_size >= 2U &&
      lifecycle->configuration->token_id_size <=
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT) {
    lifecycle->state =
        AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_INITIALIZING_SHARED_STATE;
    ++lifecycle->shared_state_attempt_count;
    shared_state_status = lifecycle->operations->initialize_shared_state(
        lifecycle->operation_context, lifecycle->configuration);
    if (shared_state_status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result,
                                         shared_state_status);
    lifecycle->shared_state_initialized = true;
  }
  if (status != NPU_RUNTIME_SUCCESS) {
    lifecycle->state =
        AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    lifecycle->last_status = status;
    ++lifecycle->configuration_wait_count;
    result->waiting_for_configuration = true;
    lifecycle_result_update(lifecycle, result);
    return status;
  }

  if (!lifecycle->activation_allowed) {
    lifecycle->state = AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!lifecycle_operations_are_valid(lifecycle->operations))
    return lifecycle_retryable_failure(lifecycle, result,
                                       NPU_RUNTIME_OUT_OF_RANGE);

  if (!lifecycle->shared_state_initialized) {
    lifecycle->state =
        AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_INITIALIZING_SHARED_STATE;
    ++lifecycle->shared_state_attempt_count;
    status = lifecycle->operations->initialize_shared_state(
        lifecycle->operation_context, lifecycle->configuration);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->shared_state_initialized = true;
  }

  if (!lifecycle->pipeline_initialized) {
    lifecycle->state =
        AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_INITIALIZING_PIPELINE;
    ++lifecycle->pipeline_attempt_count;
    status = lifecycle->operations->initialize_pipeline(
        lifecycle->operation_context, lifecycle->configuration);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->pipeline_initialized = true;
  }

  if (!lifecycle->pipeline_published) {
    lifecycle->state = AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_PUBLISHING;
    ++lifecycle->publication_attempt_count;
    status =
        lifecycle->operations->publish_pipeline(lifecycle->operation_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->pipeline_published = true;
  }

  if (!lifecycle->workers_woken) {
    lifecycle->state = AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_WAKING_WORKERS;
    ++lifecycle->wake_attempt_count;
    status = lifecycle->operations->wake_workers(
        lifecycle->operation_context,
        AN7581_WIFI_MT7996_COMPLETION_WORKER_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->workers_woken = true;
  }

  lifecycle->state = AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_shared_memory_resolve(
    struct npu_wifi_sram_allocator *allocator,
    struct an7581_wifi_mt7996_completion_shared_memory *memory) {
  struct npu_wifi_region packet_region;
  struct npu_wifi_region scratch_region;
  struct npu_wifi_region token_region;
  struct npu_wifi_region dynamic_region;

  if (allocator == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE, &packet_region) ||
      packet_region.usable_size !=
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT * sizeof(uint16_t) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING, &token_region) ||
      token_region.usable_size !=
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT * sizeof(uint16_t) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH, &scratch_region) ||
      scratch_region.usable_size != UINT32_C(0x1000) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA, &dynamic_region) ||
      dynamic_region.usable_size != NPU_WIFI_MT7996_DYNAMIC_ARENA_SIZE)
    return NPU_RUNTIME_OUT_OF_RANGE;

  memory->packet_recycle_entries =
      (volatile uint16_t *)(uintptr_t)packet_region.address;
  memory->token_entries = (volatile uint16_t *)(uintptr_t)token_region.address;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_wifi_mt7996_completion_platform_shared_state_initialize(
    struct an7581_wifi_mt7996_completion_platform *platform,
    const struct an7581_wifi_mt7996_completion_shared_memory *memory,
    uint32_t token_entry_count) {
  static const uint32_t packet_pool_mutex_handles[] = {
      UINT32_C(0x1d),
      UINT32_C(0x1c),
      UINT32_C(0x13),
      UINT32_C(0x14),
  };
  struct npu_wifi_packet_id_pool_config pool_config;
  enum npu_runtime_result status;

  if (platform == NULL || memory == NULL || memory->token_entries == NULL ||
      memory->packet_recycle_entries == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->read_hart_id == NULL ||
      token_entry_count < 2U ||
      token_entry_count > NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (platform->shared_state_external)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  (void)npu_memset(&platform->packet_pool_mutexes, 0U,
                   sizeof(platform->packet_pool_mutexes));
  (void)npu_memset(&platform->packet_pool, 0U, sizeof(platform->packet_pool));
  status = an7581_hardware_mutex_shared_bank_initialize(
      &platform->packet_pool_mutexes, platform->read_hart_id,
      platform->hart_id_context, packet_pool_mutex_handles,
      sizeof(packet_pool_mutex_handles) / sizeof(packet_pool_mutex_handles[0]));
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  pool_config = (struct npu_wifi_packet_id_pool_config){
      .token_entries = memory->token_entries,
      .recycle_entries = memory->packet_recycle_entries,
      .acquire = an7581_hardware_mutex_acquire,
      .release = an7581_hardware_mutex_release,
      .lock_context = &platform->packet_pool_mutexes,
      .diagnostic_counters = platform->diagnostic_counters,
      .token_entry_capacity = NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT,
      .token_entry_count = token_entry_count,
      .recycle_entry_count = NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT,
  };
  return npu_wifi_packet_id_pool_initialize(&platform->packet_pool,
                                            &pool_config);
}

static enum npu_runtime_result platform_initialize_shared_state(
    void *context, const struct npu_wifi_configuration *configuration) {
  struct an7581_wifi_mt7996_completion_platform *platform = context;
  struct an7581_wifi_mt7996_completion_shared_memory memory;
  enum npu_runtime_result status;

  if (platform == NULL || configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->allocator_owner == NULL ||
      platform->packet_pool_owner == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (platform->shared_state_external)
    return npu_wifi_packet_id_pool_set_token_entry_count(
        platform->packet_pool_owner, configuration->token_id_size);

  status = an7581_wifi_mt7996_completion_shared_memory_resolve(
      platform->allocator_owner, &memory);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return an7581_wifi_mt7996_completion_platform_shared_state_initialize(
      platform, &memory, configuration->token_id_size);
}

static enum npu_runtime_result platform_initialize_pipeline(
    void *context, const struct npu_wifi_configuration *configuration) {
  struct an7581_wifi_mt7996_completion_platform *platform = context;
  struct an7581_wifi_mt7996_completion_pipeline_config pipeline_config;
  enum npu_runtime_result status;

  if (platform == NULL || configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->packet_pool_owner == NULL ||
      !platform->packet_pool_owner->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&pipeline_config, 0U, sizeof(pipeline_config));
  status = an7581_wifi_mt7996_completion_pipeline_memory_resolve(
      configuration, &pipeline_config.memory);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  pipeline_config.packet_pool = platform->packet_pool_owner;
  pipeline_config.band0_diagnostic_counters =
      platform->band0_diagnostic_counters;
  pipeline_config.band1_diagnostic_counters =
      platform->band1_diagnostic_counters;
  pipeline_config.error_retry_count = &configuration->error_retry_count;
  status = an7581_wifi_mt7996_completion_readiness_resolve(
      platform->readiness, &pipeline_config.readiness);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  pipeline_config.vdma_poll_limit = platform->vdma_poll_limit;
  pipeline_config.tx_done_budget = platform->tx_done_budget;
  pipeline_config.band0_budget = platform->band0_budget;
  pipeline_config.packet_queue_producer = platform->packet_queue_producer;
  pipeline_config.packet_queue_consumers[0] =
      platform->packet_queue_consumers[0];
  pipeline_config.packet_queue_consumers[1] =
      platform->packet_queue_consumers[1];
  return an7581_wifi_mt7996_completion_pipeline_initialize(&platform->pipeline,
                                                           &pipeline_config);
}

static enum npu_runtime_result platform_publish_pipeline(void *context) {
  struct an7581_wifi_mt7996_completion_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->dispatch == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return an7581_wifi_mt7996_completion_dispatch_publish(platform->dispatch,
                                                        &platform->pipeline);
}

static enum npu_runtime_result platform_wake_workers(void *context,
                                                     uint32_t hart_mask) {
  struct an7581_wifi_mt7996_completion_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->wake_workers == NULL ||
      hart_mask != AN7581_WIFI_MT7996_COMPLETION_WORKER_HART_MASK)
    return NPU_RUNTIME_REJECTED;
  return platform->wake_workers(platform->wake_context, hart_mask);
}

static const struct an7581_wifi_mt7996_completion_lifecycle_operations
    platform_operations = {
        .initialize_shared_state = platform_initialize_shared_state,
        .initialize_pipeline = platform_initialize_pipeline,
        .publish_pipeline = platform_publish_pipeline,
        .wake_workers = platform_wake_workers,
};

static bool external_shared_state_is_valid(
    const struct an7581_wifi_mt7996_completion_platform_config *config) {
  const struct npu_wifi_sram_allocator *allocator = config->shared_allocator;
  const struct npu_wifi_packet_id_pool *pool = config->shared_packet_pool;

  if (allocator == NULL || pool == NULL || !pool->initialized ||
      pool->token_entries == NULL || pool->recycle_entries == NULL ||
      pool->token_entry_capacity != NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      allocator->allocation_count < 4U)
    return false;

  return allocator->allocations[0].type ==
             NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE &&
         allocator->allocations[0].address == UINT32_C(0x3e800000) &&
         allocator->allocations[1].type == NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING &&
         allocator->allocations[1].address == UINT32_C(0x3e808000) &&
         allocator->allocations[2].type ==
             NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH &&
         allocator->allocations[2].address == UINT32_C(0x3e816000) &&
         allocator->allocations[3].type == NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA &&
         allocator->allocations[3].address == UINT32_C(0x3e817000);
}

enum npu_runtime_result an7581_wifi_mt7996_completion_platform_initialize(
    struct an7581_wifi_mt7996_completion_platform *platform,
    const struct an7581_wifi_mt7996_completion_platform_config *config) {
  if (platform == NULL || config == NULL || config->dispatch == NULL ||
      config->readiness == NULL || config->read_hart_id == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->vdma_poll_limit == 0U || config->tx_done_budget == 0U ||
      config->tx_done_budget > NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT ||
      config->band0_budget == 0U ||
      config->band0_budget > NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET ||
      (uint32_t)config->packet_queue_producer >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)config->packet_queue_consumers[0] >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)config->packet_queue_consumers[1] >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if ((config->shared_allocator == NULL) !=
      (config->shared_packet_pool == NULL))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->shared_packet_pool != NULL &&
      !external_shared_state_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->dispatch = config->dispatch;
  platform->readiness = config->readiness;
  platform->wake_workers = config->wake_workers;
  platform->wake_context = config->wake_context;
  platform->read_hart_id = config->read_hart_id;
  platform->hart_id_context = config->hart_id_context;
  platform->vdma_poll_limit = config->vdma_poll_limit;
  platform->tx_done_budget = config->tx_done_budget;
  platform->band0_budget = config->band0_budget;
  platform->packet_queue_producer = config->packet_queue_producer;
  platform->packet_queue_consumers[0] = config->packet_queue_consumers[0];
  platform->packet_queue_consumers[1] = config->packet_queue_consumers[1];
  platform->shared_state_external = config->shared_packet_pool != NULL;
  platform->allocator_owner = platform->shared_state_external
                                  ? config->shared_allocator
                                  : &platform->allocator;
  platform->packet_pool_owner = platform->shared_state_external
                                    ? config->shared_packet_pool
                                    : &platform->packet_pool;
  platform->diagnostic_counters = config->diagnostic_counters;
  platform->band0_diagnostic_counters = config->band0_diagnostic_counters;
  platform->band1_diagnostic_counters = config->band1_diagnostic_counters;
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

const struct an7581_wifi_mt7996_completion_lifecycle_operations *
an7581_wifi_mt7996_completion_platform_operations(void) {
  return &platform_operations;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_tx_fast_path_lifecycle.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/wifi_mt7996_completion_runtime.h"
#include "an7581/runtime/memory.h"

static bool lifecycle_operations_are_valid(
    const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations
        *operations) {
  return operations != NULL && operations->initialize != NULL &&
         operations->publish != NULL && operations->wake_worker != NULL;
}

static void interface_readiness(
    const struct npu_wifi_interface_configuration *interface,
    uint32_t required_bit, uint32_t staging_size,
    struct an7581_wifi_mt7996_tx_fast_path_configuration_readiness *readiness) {
  uint32_t local_staging_address;

  if ((interface->valid_fields & (NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS |
                                  NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE)) !=
      (NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS |
       NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE)) {
    readiness->missing |= required_bit;
  } else if (interface->tx_ring_pcie_address == 0U ||
             (interface->tx_ring_pcie_address & (sizeof(uint32_t) - 1U)) !=
                 0U ||
             interface->tx_ring_pcie_address >
                 UINT32_MAX - sizeof(struct npu_wifi_tx_ring_registers) ||
             !an7581_dma_buffer_map(interface->tx_buffer_space_base,
                                    staging_size,
                                    NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE,
                                    &local_staging_address)) {
    readiness->invalid |= required_bit;
  }
}

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_configuration_readiness(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_tx_fast_path_configuration_readiness *readiness) {
  uint32_t maximum_packet_offset;

  if (configuration == NULL || readiness == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(readiness, 0U, sizeof(*readiness));
  if (!configuration->tx_packet_buffer_address_valid) {
    readiness->missing |=
        AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_PACKET_BUFFER;
  } else if (configuration->tx_packet_buffer_address_out_of_range ||
             configuration->tx_packet_buffer_address == 0U ||
             (configuration->tx_packet_buffer_address &
              (NPU_WIFI_TDM_RX_PACKET_SIZE - 1U)) != 0U) {
    readiness->invalid |=
        AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_PACKET_BUFFER;
  }

  if (!configuration->tx_buffer_check_address_valid) {
    readiness->missing |= AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_STATE;
  } else if (configuration->tx_buffer_check_address == 0U ||
             (configuration->tx_buffer_check_address &
              (sizeof(uint16_t) - 1U)) != 0U) {
    readiness->invalid |= AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_STATE;
  }

  if (!configuration->token_id_size_valid) {
    readiness->missing |= AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_COUNT;
  } else if (configuration->token_id_size <=
                 NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT ||
             configuration->token_id_size >
                 NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT) {
    readiness->invalid |= AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_COUNT;
  } else {
    maximum_packet_offset =
        (configuration->token_id_size - 1U) * NPU_WIFI_TDM_RX_PACKET_SIZE;
    if (configuration->tx_packet_buffer_address >
        UINT32_MAX - maximum_packet_offset)
      readiness->invalid |=
          AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_PACKET_BUFFER;
  }

  interface_readiness(
      &configuration
           ->interface[AN7581_WIFI_MT7996_TX_SLOW_PATH_BAND0_INTERFACE],
      AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_BAND0,
      NPU_WIFI_MT7996_TX_BAND0_DESCRIPTOR_COUNT *
          NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE,
      readiness);
  interface_readiness(
      &configuration
           ->interface[AN7581_WIFI_MT7996_TX_SLOW_PATH_BAND2_INTERFACE],
      AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_BAND2,
      NPU_WIFI_MT7996_TX_SECONDARY_DESCRIPTOR_COUNT *
          NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE,
      readiness);

  if (readiness->invalid != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (readiness->missing != 0U)
    return NPU_RUNTIME_EMPTY;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_lifecycle_initialize(
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_config *config) {
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
      AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAITING_FOR_CONFIGURATION;
  lifecycle->last_status = NPU_RUNTIME_EMPTY;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static void lifecycle_result_update(
    const struct an7581_wifi_mt7996_tx_fast_path_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->fast_path_initialized = lifecycle->fast_path_initialized;
  result->fast_path_published = lifecycle->fast_path_published;
  result->worker_woken = lifecycle->worker_woken;
  result->active =
      lifecycle->state == AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result lifecycle_retryable_failure(
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result *result,
    enum npu_runtime_result status) {
  lifecycle->state =
      AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_lifecycle_step(
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result *result) {
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized || lifecycle->configuration == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  status = an7581_wifi_mt7996_tx_fast_path_configuration_readiness(
      lifecycle->configuration, &result->readiness);
  if (status != NPU_RUNTIME_SUCCESS) {
    lifecycle->state =
        AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    lifecycle->last_status = status;
    ++lifecycle->configuration_wait_count;
    result->waiting_for_configuration = true;
    lifecycle_result_update(lifecycle, result);
    return status;
  }

  if (!lifecycle->activation_allowed) {
    lifecycle->state =
        AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!lifecycle_operations_are_valid(lifecycle->operations))
    return lifecycle_retryable_failure(lifecycle, result,
                                       NPU_RUNTIME_OUT_OF_RANGE);

  if (!lifecycle->fast_path_initialized) {
    lifecycle->state = AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_INITIALIZING;
    ++lifecycle->initialization_attempt_count;
    status = lifecycle->operations->initialize(lifecycle->operation_context,
                                               lifecycle->configuration);
    if (status == NPU_RUNTIME_EMPTY) {
      lifecycle->state =
          AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAITING_FOR_SHARED_STATE;
      lifecycle->last_status = status;
      ++lifecycle->shared_state_wait_count;
      result->waiting_for_shared_state = true;
      lifecycle_result_update(lifecycle, result);
      return status;
    }
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->fast_path_initialized = true;
  }

  if (!lifecycle->fast_path_published) {
    lifecycle->state = AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_PUBLISHING;
    ++lifecycle->publication_attempt_count;
    status = lifecycle->operations->publish(lifecycle->operation_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->fast_path_published = true;
  }

  if (!lifecycle->worker_woken) {
    lifecycle->state = AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAKING_WORKER;
    ++lifecycle->wake_attempt_count;
    status = lifecycle->operations->wake_worker(lifecycle->operation_context,
                                                AN7581_CORE2_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->worker_woken = true;
  }

  lifecycle->state = AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

static bool shared_state_is_ready(
    const struct an7581_wifi_mt7996_tx_fast_path_platform *platform,
    const struct npu_wifi_configuration *configuration,
    uint32_t *dynamic_base) {
  const struct npu_wifi_sram_allocator *allocator = platform->shared_allocator;
  const struct npu_wifi_packet_id_pool *pool = platform->shared_packet_pool;

  if (allocator == NULL || pool == NULL || dynamic_base == NULL ||
      !pool->initialized || pool->token_entries == NULL ||
      pool->recycle_entries == NULL ||
      pool->token_entry_capacity != NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      pool->token_entry_count != configuration->token_id_size ||
      allocator->allocation_count < 4U)
    return false;

  if (allocator->allocations[0].type !=
          NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE ||
      allocator->allocations[0].address != UINT32_C(0x3e800000) ||
      allocator->allocations[1].type != NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING ||
      allocator->allocations[1].address != UINT32_C(0x3e808000) ||
      allocator->allocations[2].type !=
          NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH ||
      allocator->allocations[2].address != UINT32_C(0x3e816000) ||
      allocator->allocations[3].type != NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA ||
      allocator->allocations[3].address != UINT32_C(0x3e817000))
    return false;

  *dynamic_base = allocator->allocations[3].address;
  return true;
}

static enum npu_runtime_result platform_initialize_fast_path(
    void *context, const struct npu_wifi_configuration *configuration) {
  struct an7581_wifi_mt7996_tx_fast_path_platform *platform = context;
  struct an7581_wifi_tx_fast_path_config config;
  uint32_t dynamic_base;

  if (platform == NULL || configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->dispatch == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!shared_state_is_ready(platform, configuration, &dynamic_base))
    return NPU_RUNTIME_EMPTY;

  config = (struct an7581_wifi_tx_fast_path_config){
      .allocator = platform->shared_allocator,
      .token_pool = platform->shared_packet_pool,
      .wifi_configuration = configuration,
      .initialization_complete = &platform->readiness->offload_initialized,
      .packet_space_ready = &platform->readiness->tx_done_enabled,
      .configuration_state = &platform->readiness->tx_configuration_state,
      .band0_diagnostic_counters = platform->band0_diagnostic_counters,
      .band1_diagnostic_counters = platform->band1_diagnostic_counters,
      .dynamic_base = dynamic_base,
      .token_state_count = configuration->token_id_size,
      .vdma_poll_limit = platform->vdma_poll_limit,
  };
  return an7581_wifi_tx_fast_path_platform_initialize(&platform->fast_path,
                                                      &config);
}

static enum npu_runtime_result platform_publish_fast_path(void *context) {
  struct an7581_wifi_mt7996_tx_fast_path_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || !platform->fast_path.initialized ||
      platform->dispatch == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return an7581_core2_dispatch_publish_wifi_tx_fast_path(
      platform->dispatch, &platform->fast_path.runtime);
}

static enum npu_runtime_result platform_wake_worker(void *context,
                                                    uint32_t hart_mask) {
  struct an7581_wifi_mt7996_tx_fast_path_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized || platform->wake_worker == NULL ||
      hart_mask != AN7581_CORE2_HART_MASK)
    return NPU_RUNTIME_REJECTED;
  return platform->wake_worker(platform->wake_context, hart_mask);
}

static const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations
    platform_operations = {
        .initialize = platform_initialize_fast_path,
        .publish = platform_publish_fast_path,
        .wake_worker = platform_wake_worker,
};

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_platform_initialize(
    struct an7581_wifi_mt7996_tx_fast_path_platform *platform,
    const struct an7581_wifi_mt7996_tx_fast_path_platform_config *config) {
  if (platform == NULL || config == NULL || config->dispatch == NULL ||
      config->shared_allocator == NULL || config->shared_packet_pool == NULL ||
      config->readiness == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->vdma_poll_limit == 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->dispatch = config->dispatch;
  platform->shared_allocator = config->shared_allocator;
  platform->shared_packet_pool = config->shared_packet_pool;
  platform->readiness = config->readiness;
  platform->band0_diagnostic_counters = config->band0_diagnostic_counters;
  platform->band1_diagnostic_counters = config->band1_diagnostic_counters;
  platform->wake_worker = config->wake_worker;
  platform->wake_context = config->wake_context;
  platform->vdma_poll_limit = config->vdma_poll_limit;
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations *
an7581_wifi_mt7996_tx_fast_path_platform_operations(void) {
  return &platform_operations;
}

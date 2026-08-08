/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/token_id_reset.h"

#include "an7581/platform/dma.h"

_Static_assert(sizeof(struct an7581_qdma_descriptor) ==
                   NPU_WIFI_TOKEN_ID_TDM_DESCRIPTOR_SIZE,
               "TDM descriptor layout changed");

static bool reset_configuration_is_valid(
    const struct npu_wifi_token_id_reset_config *config) {
  uint32_t ring_index;

  if (config == NULL || config->scratch_entries == NULL ||
      config->token_states == NULL || config->packet_buffer_base == 0U ||
      config->scratch_entry_count != NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT)
    return false;
  if (((uintptr_t)config->scratch_entries & (sizeof(uint16_t) - 1U)) != 0U ||
      ((uintptr_t)config->token_states & (sizeof(uint16_t) - 1U)) != 0U)
    return false;
  for (ring_index = 0U; ring_index < NPU_WIFI_TOKEN_ID_TDM_RING_COUNT;
       ++ring_index) {
    if (config->tdm_rings[ring_index] == NULL ||
        ((uintptr_t)config->tdm_rings[ring_index] & (sizeof(uint32_t) - 1U)) !=
            0U)
      return false;
  }
  return true;
}

static bool
pool_configuration_is_valid(const struct npu_wifi_packet_id_pool *pool) {
  return pool != NULL && pool->initialized && pool->token_entries != NULL &&
         pool->acquire != NULL && pool->release != NULL &&
         pool->token_entry_capacity ==
             NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT &&
         pool->token_entry_count > NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT &&
         pool->token_entry_count <= pool->token_entry_capacity;
}

static enum npu_runtime_result
release_force_reset_locks(struct npu_wifi_packet_id_pool *pool,
                          bool producer_held, enum npu_runtime_result status) {
  enum npu_runtime_result release_status;

  if (producer_held) {
    release_status =
        pool->release(pool->lock_context, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK);
    if (status == NPU_RUNTIME_SUCCESS)
      status = release_status;
  }
  release_status =
      pool->release(pool->lock_context, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK);
  if (status == NPU_RUNTIME_SUCCESS)
    status = release_status;
  return status;
}

static void
clear_token_states(const struct npu_wifi_token_id_reset_config *config,
                   uint32_t token_count) {
  uint32_t entry_index;

  for (entry_index = 0U; entry_index < token_count; ++entry_index)
    config->token_states[entry_index] = 0U;
}

enum npu_runtime_result npu_wifi_token_id_collect_tdm(
    const struct npu_wifi_token_id_reset_config *config, uint32_t token_count,
    uint32_t *collected_count) {
  const volatile struct an7581_qdma_descriptor *descriptors;
  uint32_t buffer_address;
  uint32_t descriptor_index;
  uint32_t entry_index = 0U;
  uint32_t ring_index;
  uint32_t token_id;

  if (config == NULL || collected_count == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!reset_configuration_is_valid(config) ||
      token_count <= NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT ||
      token_count > NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      config->token_state_count < token_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (entry_index = 0U; entry_index < config->scratch_entry_count;
       ++entry_index)
    config->scratch_entries[entry_index] = NPU_WIFI_TOKEN_ID_UNUSED;

  entry_index = 0U;
  for (ring_index = 0U; ring_index < NPU_WIFI_TOKEN_ID_TDM_RING_COUNT;
       ++ring_index) {
    descriptors = config->tdm_rings[ring_index];
    for (descriptor_index = 0U;
         descriptor_index < NPU_WIFI_TOKEN_ID_TDM_RING_ENTRY_COUNT;
         ++descriptor_index) {
      buffer_address = descriptors[descriptor_index].buffer_address;
      if (buffer_address < config->packet_buffer_base ||
          (buffer_address - config->packet_buffer_base) %
                  NPU_WIFI_TOKEN_ID_PACKET_STRIDE !=
              0U)
        return NPU_RUNTIME_OUT_OF_RANGE;
      token_id = (buffer_address - config->packet_buffer_base) /
                 NPU_WIFI_TOKEN_ID_PACKET_STRIDE;
      if (token_id >= token_count)
        return NPU_RUNTIME_OUT_OF_RANGE;
      config->scratch_entries[entry_index] = (uint16_t)token_id;
      ++entry_index;
    }
  }

  an7581_dma_memory_barrier();
  *collected_count = entry_index;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_token_id_pool_force_reset(
    struct npu_wifi_packet_id_pool *pool,
    const struct npu_wifi_token_id_reset_config *config) {
  uint32_t collected_count;
  uint32_t entry_index;
  uint32_t output_index;
  uint16_t token_id;
  enum npu_runtime_result status;

  if (pool == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool_configuration_is_valid(pool) ||
      !reset_configuration_is_valid(config) ||
      config->token_state_count < pool->token_entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = pool->acquire(pool->lock_context, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = pool->acquire(pool->lock_context, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return release_force_reset_locks(pool, false, status);

  status = npu_wifi_token_id_collect_tdm(config, pool->token_entry_count,
                                         &collected_count);
  if (status != NPU_RUNTIME_SUCCESS)
    return release_force_reset_locks(pool, true, status);

  clear_token_states(config, pool->token_entry_count);
  for (entry_index = 0U; entry_index < collected_count; ++entry_index) {
    token_id = config->scratch_entries[entry_index];
    if (config->token_states[token_id] == NPU_WIFI_TOKEN_ID_STATE_TDM) {
      clear_token_states(config, pool->token_entry_count);
      return release_force_reset_locks(pool, true, NPU_RUNTIME_REJECTED);
    }
    config->token_states[token_id] = NPU_WIFI_TOKEN_ID_STATE_TDM;
  }
  for (entry_index = 0U; entry_index < collected_count; ++entry_index)
    pool->token_entries[entry_index] = config->scratch_entries[entry_index];

  output_index = collected_count;
  for (entry_index = 0U; entry_index < pool->token_entry_count; ++entry_index) {
    if (config->token_states[entry_index] != NPU_WIFI_TOKEN_ID_STATE_TDM) {
      pool->token_entries[output_index] = (uint16_t)entry_index;
      ++output_index;
    }
  }
  if (output_index != pool->token_entry_count)
    return release_force_reset_locks(pool, true, NPU_RUNTIME_REJECTED);

  an7581_dma_memory_barrier();
  pool->token_consumer = (uint16_t)collected_count;
  pool->token_producer = 0U;
  ++pool->token_force_reset_count;
  an7581_dma_memory_barrier();
  return release_force_reset_locks(pool, true, NPU_RUNTIME_SUCCESS);
}

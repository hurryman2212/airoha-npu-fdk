/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/packet_id_pool.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool
pool_configuration_is_valid(const struct npu_wifi_packet_id_pool *pool) {
  return pool != NULL && pool->token_entries != NULL &&
         pool->recycle_entries != NULL && pool->acquire != NULL &&
         pool->release != NULL &&
         ((uintptr_t)pool->token_entries & (sizeof(uint16_t) - 1U)) == 0U &&
         ((uintptr_t)pool->recycle_entries & (sizeof(uint16_t) - 1U)) == 0U &&
         pool->token_entry_capacity ==
             NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT &&
         pool->token_entry_count >= 2U &&
         pool->token_entry_count <= pool->token_entry_capacity;
}

static enum npu_runtime_result
release_lock(struct npu_wifi_packet_id_pool *pool, uint32_t lock_index,
             enum npu_runtime_result status) {
  enum npu_runtime_result release_status =
      pool->release(pool->lock_context, lock_index);

  if (status == NPU_RUNTIME_SUCCESS)
    return release_status;
  return status;
}

static enum npu_runtime_result
release_reset_locks(struct npu_wifi_packet_id_pool *pool,
                    uint32_t acquired_count, enum npu_runtime_result status) {
  uint32_t lock_index;

  for (lock_index = 0U; lock_index < acquired_count; ++lock_index)
    status = release_lock(pool, lock_index, status);
  return status;
}

static uint16_t advance_index(uint16_t index, uint32_t entry_count) {
  uint32_t next_index = (uint32_t)index + 1U;

  if (next_index == entry_count)
    next_index = 0U;
  return (uint16_t)next_index;
}

static void initialize_recycle_ring(struct npu_wifi_packet_id_pool *pool) {
  uint32_t entry_index;

  for (entry_index = 0U; entry_index < NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT;
       ++entry_index)
    pool->recycle_entries[entry_index] = (uint16_t)entry_index;
  an7581_dma_memory_barrier();
  pool->recycle_consumer = 0U;
  pool->recycle_producer = 0U;
  an7581_dma_memory_barrier();
}

enum npu_runtime_result
npu_wifi_packet_id_pool_allocate(struct npu_wifi_packet_id_pool *pool,
                                 uint16_t *packet_id) {
  uint16_t next_consumer;
  uint16_t producer;
  enum npu_runtime_result status;

  if (pool == NULL || packet_id == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool_configuration_is_valid(pool) || !pool->initialized ||
      (uint32_t)pool->recycle_consumer >=
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT ||
      (uint32_t)pool->recycle_producer >=
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = pool->acquire(pool->lock_context,
                         NPU_WIFI_PACKET_ID_RECYCLE_CONSUMER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  producer = pool->recycle_producer;
  an7581_dma_memory_barrier();
  next_consumer = advance_index(pool->recycle_consumer,
                                NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT);
  if (next_consumer == producer) {
    if (pool->diagnostic_counters != NULL)
      ++pool->diagnostic_counters->packet_id_allocation_failures;
    return release_lock(pool, NPU_WIFI_PACKET_ID_RECYCLE_CONSUMER_LOCK,
                        NPU_RUNTIME_EMPTY);
  }

  *packet_id = pool->recycle_entries[pool->recycle_consumer];
  if ((uint32_t)*packet_id >= NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT)
    return release_lock(pool, NPU_WIFI_PACKET_ID_RECYCLE_CONSUMER_LOCK,
                        NPU_RUNTIME_OUT_OF_RANGE);
  pool->recycle_consumer = next_consumer;
  ++pool->packet_allocation_count;
  if (pool->diagnostic_counters != NULL)
    ++pool->diagnostic_counters->packet_id_allocations;
  an7581_dma_memory_barrier();
  return release_lock(pool, NPU_WIFI_PACKET_ID_RECYCLE_CONSUMER_LOCK,
                      NPU_RUNTIME_SUCCESS);
}

enum npu_runtime_result
npu_wifi_packet_id_pool_release(struct npu_wifi_packet_id_pool *pool,
                                uint16_t packet_id) {
  uint16_t next_producer;
  enum npu_runtime_result status;

  if (pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool_configuration_is_valid(pool) || !pool->initialized ||
      (uint32_t)packet_id >= NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT ||
      (uint32_t)pool->recycle_consumer >=
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT ||
      (uint32_t)pool->recycle_producer >=
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = pool->acquire(pool->lock_context,
                         NPU_WIFI_PACKET_ID_RECYCLE_PRODUCER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  next_producer = advance_index(pool->recycle_producer,
                                NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT);
  if (next_producer == pool->recycle_consumer)
    return release_lock(pool, NPU_WIFI_PACKET_ID_RECYCLE_PRODUCER_LOCK,
                        NPU_RUNTIME_FULL);

  pool->recycle_entries[pool->recycle_producer] = packet_id;
  an7581_dma_memory_barrier();
  pool->recycle_producer = next_producer;
  ++pool->packet_release_count;
  if (pool->diagnostic_counters != NULL)
    ++pool->diagnostic_counters->packet_id_releases;
  an7581_dma_memory_barrier();
  return release_lock(pool, NPU_WIFI_PACKET_ID_RECYCLE_PRODUCER_LOCK,
                      NPU_RUNTIME_SUCCESS);
}

enum npu_runtime_result
npu_wifi_token_id_pool_allocate(struct npu_wifi_packet_id_pool *pool,
                                uint16_t *token_id) {
  uint16_t next_consumer;
  uint16_t producer;
  enum npu_runtime_result status;

  if (pool == NULL || token_id == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool_configuration_is_valid(pool) || !pool->initialized ||
      (uint32_t)pool->token_consumer >= pool->token_entry_count ||
      (uint32_t)pool->token_producer >= pool->token_entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = pool->acquire(pool->lock_context, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  producer = pool->token_producer;
  an7581_dma_memory_barrier();
  next_consumer = advance_index(pool->token_consumer, pool->token_entry_count);
  if (next_consumer == producer) {
    if (pool->diagnostic_counters != NULL)
      ++pool->diagnostic_counters->token_id_allocation_failures;
    return release_lock(pool, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK,
                        NPU_RUNTIME_EMPTY);
  }

  *token_id = pool->token_entries[pool->token_consumer];
  if ((uint32_t)*token_id >= pool->token_entry_count)
    return release_lock(pool, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK,
                        NPU_RUNTIME_OUT_OF_RANGE);
  pool->token_consumer = next_consumer;
  ++pool->token_allocation_count;
  if (pool->diagnostic_counters != NULL)
    ++pool->diagnostic_counters->token_id_allocations;
  an7581_dma_memory_barrier();
  return release_lock(pool, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK,
                      NPU_RUNTIME_SUCCESS);
}

enum npu_runtime_result
npu_wifi_token_id_pool_release(struct npu_wifi_packet_id_pool *pool,
                               uint16_t token_id) {
  uint16_t next_producer;
  enum npu_runtime_result status;

  if (pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool_configuration_is_valid(pool) || !pool->initialized ||
      (uint32_t)token_id >= pool->token_entry_count ||
      (uint32_t)pool->token_consumer >= pool->token_entry_count ||
      (uint32_t)pool->token_producer >= pool->token_entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = pool->acquire(pool->lock_context, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  next_producer = advance_index(pool->token_producer, pool->token_entry_count);
  if (next_producer == pool->token_consumer)
    return release_lock(pool, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK,
                        NPU_RUNTIME_FULL);

  pool->token_entries[pool->token_producer] = token_id;
  an7581_dma_memory_barrier();
  pool->token_producer = next_producer;
  ++pool->token_release_count;
  if (pool->diagnostic_counters != NULL)
    ++pool->diagnostic_counters->token_id_releases;
  an7581_dma_memory_barrier();
  return release_lock(pool, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK,
                      NPU_RUNTIME_SUCCESS);
}

enum npu_runtime_result npu_wifi_packet_id_pool_set_token_entry_count(
    struct npu_wifi_packet_id_pool *pool, uint32_t token_entry_count) {
  enum npu_runtime_result status;

  if (pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool_configuration_is_valid(pool) || !pool->initialized ||
      token_entry_count < 2U ||
      token_entry_count > NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (token_entry_count == pool->token_entry_count)
    return NPU_RUNTIME_SUCCESS;

  status = pool->acquire(pool->lock_context, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = pool->acquire(pool->lock_context, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK);
  if (status != NPU_RUNTIME_SUCCESS)
    return release_lock(pool, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK, status);

  if (pool->token_consumer != 0U || pool->token_producer != 0U ||
      pool->token_allocation_count != 0U || pool->token_release_count != 0U ||
      pool->token_force_reset_count != 0U) {
    status = NPU_RUNTIME_OWNERSHIP_ERROR;
  } else {
    pool->token_entry_count = token_entry_count;
    an7581_dma_memory_barrier();
    status = NPU_RUNTIME_SUCCESS;
  }

  status = release_lock(pool, NPU_WIFI_TOKEN_ID_PRODUCER_LOCK, status);
  return release_lock(pool, NPU_WIFI_TOKEN_ID_CONSUMER_LOCK, status);
}

enum npu_runtime_result npu_wifi_packet_id_pool_reset(void *context) {
  struct npu_wifi_packet_id_pool *pool = context;
  uint32_t acquired_count;
  enum npu_runtime_result status;

  if (!pool_configuration_is_valid(pool))
    return pool == NULL ? NPU_RUNTIME_INVALID_ARGUMENT
                        : NPU_RUNTIME_OUT_OF_RANGE;

  for (acquired_count = 0U;
       acquired_count < NPU_WIFI_PACKET_ID_POOL_RESET_LOCK_COUNT;
       ++acquired_count) {
    status = pool->acquire(pool->lock_context, acquired_count);
    if (status != NPU_RUNTIME_SUCCESS)
      return release_reset_locks(pool, acquired_count, status);
  }

  pool->initialized = false;
  an7581_dma_memory_barrier();
  initialize_recycle_ring(pool);
  pool->initialized = true;
  ++pool->reset_count;

  return release_reset_locks(pool, acquired_count, NPU_RUNTIME_SUCCESS);
}

enum npu_runtime_result npu_wifi_packet_id_pool_initialize(
    struct npu_wifi_packet_id_pool *pool,
    const struct npu_wifi_packet_id_pool_config *config) {
  uint32_t entry_index;

  if (pool == NULL || config == NULL || config->token_entries == NULL ||
      config->recycle_entries == NULL || config->acquire == NULL ||
      config->release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)config->token_entries & (sizeof(uint16_t) - 1U)) != 0U ||
      ((uintptr_t)config->recycle_entries & (sizeof(uint16_t) - 1U)) != 0U ||
      config->token_entry_capacity !=
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      config->token_entry_count < 2U ||
      config->token_entry_count > config->token_entry_capacity ||
      config->recycle_entry_count != NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(pool, 0U, sizeof(*pool));
  pool->token_entries = config->token_entries;
  pool->recycle_entries = config->recycle_entries;
  pool->acquire = config->acquire;
  pool->release = config->release;
  pool->lock_context = config->lock_context;
  pool->diagnostic_counters = config->diagnostic_counters;
  pool->token_entry_capacity = config->token_entry_capacity;
  pool->token_entry_count = config->token_entry_count;
  for (entry_index = 0U; entry_index < pool->token_entry_capacity;
       ++entry_index)
    pool->token_entries[entry_index] = (uint16_t)entry_index;
  an7581_dma_memory_barrier();
  pool->token_consumer = 0U;
  pool->token_producer = 0U;
  an7581_dma_memory_barrier();
  initialize_recycle_ring(pool);
  pool->initialized = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

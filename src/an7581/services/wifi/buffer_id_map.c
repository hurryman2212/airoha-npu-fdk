/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/buffer_id_map.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static uint16_t advance_index(uint16_t index) {
  uint32_t next_index = (uint32_t)index + 1U;

  if (next_index == NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT)
    next_index = 0U;
  return (uint16_t)next_index;
}

static enum npu_runtime_result
populate_identity_map(struct npu_wifi_buffer_id_map *map) {
  uint32_t index;

  for (index = 0U; index < NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT; ++index)
    map->entries[index] = (uint16_t)index;

  an7581_dma_memory_barrier();
  map->consumer_index = 0U;
  map->producer_index = 0U;
  map->last_allocation_status = NPU_RUNTIME_SUCCESS;
  map->last_release_status = NPU_RUNTIME_SUCCESS;
  an7581_dma_memory_barrier();
  map->initialized = true;
  ++map->reset_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_buffer_id_map_initialize(
    struct npu_wifi_buffer_id_map *map, volatile uint16_t *entries,
    uint32_t entry_count,
    volatile struct npu_wifi_mt7996_band2_diagnostic_counters
        *diagnostic_counters) {
  if (map == NULL || entries == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)entries & (sizeof(uint16_t) - 1U)) != 0U ||
      entry_count != NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(map, 0U, sizeof(*map));
  map->entries = entries;
  map->diagnostic_counters = diagnostic_counters;
  return populate_identity_map(map);
}

enum npu_runtime_result
npu_wifi_buffer_id_map_allocate(struct npu_wifi_buffer_id_map *map,
                                uint16_t *buffer_id) {
  uint16_t next_consumer;
  uint16_t producer;
  enum npu_runtime_result status;

  if (map == NULL || buffer_id == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!map->initialized || map->entries == NULL ||
      (uint32_t)map->consumer_index >= NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT ||
      (uint32_t)map->producer_index >= NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT) {
    status = NPU_RUNTIME_OUT_OF_RANGE;
  } else {
    producer = map->producer_index;
    an7581_dma_memory_barrier();
    next_consumer = advance_index(map->consumer_index);
    if (next_consumer == producer) {
      status = NPU_RUNTIME_EMPTY;
    } else {
      *buffer_id = map->entries[map->consumer_index];
      if ((uint32_t)*buffer_id >= NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT) {
        status = NPU_RUNTIME_OUT_OF_RANGE;
      } else {
        map->consumer_index = next_consumer;
        ++map->allocation_count;
        an7581_dma_memory_barrier();
        status = NPU_RUNTIME_SUCCESS;
      }
    }
  }

  map->last_allocation_status = status;
  if (status == NPU_RUNTIME_SUCCESS) {
    if (map->diagnostic_counters != NULL)
      ++map->diagnostic_counters->msdu_page_id_allocations;
  } else {
    ++map->allocation_failure_count;
    if (map->diagnostic_counters != NULL)
      ++map->diagnostic_counters->msdu_page_id_allocation_failures;
  }
  return status;
}

enum npu_runtime_result
npu_wifi_buffer_id_map_release(struct npu_wifi_buffer_id_map *map,
                               uint16_t buffer_id) {
  uint16_t next_producer;
  enum npu_runtime_result status;

  if (map == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!map->initialized || map->entries == NULL ||
      (uint32_t)buffer_id >= NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT ||
      (uint32_t)map->consumer_index >= NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT ||
      (uint32_t)map->producer_index >= NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT) {
    status = NPU_RUNTIME_OUT_OF_RANGE;
  } else {
    next_producer = advance_index(map->producer_index);
    if (next_producer == map->consumer_index) {
      status = NPU_RUNTIME_FULL;
    } else {
      map->entries[map->producer_index] = buffer_id;
      an7581_dma_memory_barrier();
      map->producer_index = next_producer;
      ++map->release_count;
      an7581_dma_memory_barrier();
      status = NPU_RUNTIME_SUCCESS;
    }
  }

  map->last_release_status = status;
  if (status != NPU_RUNTIME_SUCCESS)
    ++map->release_failure_count;
  return status;
}

enum npu_runtime_result npu_wifi_buffer_id_map_reset(void *context) {
  struct npu_wifi_buffer_id_map *map = context;

  if (map == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!map->initialized || map->entries == NULL ||
      ((uintptr_t)map->entries & (sizeof(uint16_t) - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  map->initialized = false;
  an7581_dma_memory_barrier();
  return populate_identity_map(map);
}

static bool allocate_buffer_id(void *context, uint16_t *buffer_id) {
  return npu_wifi_buffer_id_map_allocate(context, buffer_id) ==
         NPU_RUNTIME_SUCCESS;
}

static void release_buffer_id(void *context, uint16_t buffer_id) {
  (void)npu_wifi_buffer_id_map_release(context, buffer_id);
}

const struct npu_wifi_rx_buffer_operations npu_wifi_buffer_id_map_operations = {
    .allocate = allocate_buffer_id,
    .release = release_buffer_id,
};

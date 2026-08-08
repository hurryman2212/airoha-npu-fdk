/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_packet_queue.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct npu_wifi_mt7996_packet_queue_entry) ==
                   NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_SIZE,
               "MT7996 packet-queue entry layout changed");
_Static_assert(offsetof(struct npu_wifi_mt7996_packet_queue_entry,
                        flow_value) == 4U,
               "MT7996 packet-queue flow-value offset changed");
_Static_assert(offsetof(struct npu_wifi_mt7996_packet_queue_entry,
                        total_length) == 6U,
               "MT7996 packet-queue total-length offset changed");
_Static_assert(offsetof(struct npu_wifi_mt7996_packet_queue_entry,
                        fragment_length) == 8U,
               "MT7996 packet-queue fragment-length offset changed");
_Static_assert(offsetof(struct npu_wifi_mt7996_packet_queue_entry, flags) ==
                   10U,
               "MT7996 packet-queue flags offset changed");
_Static_assert(offsetof(struct npu_wifi_mt7996_packet_queue_entry, route) ==
                   11U,
               "MT7996 packet-queue route offset changed");
_Static_assert(sizeof(struct npu_wifi_mt7996_fragment_queue_entry) ==
                   NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_SIZE,
               "MT7996 fragment-queue entry layout changed");

static bool pointer_is_aligned(const volatile void *pointer) {
  return ((uintptr_t)pointer & (sizeof(uint32_t) - 1U)) == 0U;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

static enum npu_runtime_result
release_lock(struct npu_wifi_mt7996_packet_queue *queue,
             enum npu_runtime_result status) {
  enum npu_runtime_result release_status = queue->release(queue->lock_context);

  if (status == NPU_RUNTIME_SUCCESS)
    return release_status;
  return status;
}

enum npu_runtime_result npu_wifi_mt7996_packet_queue_initialize(
    struct npu_wifi_mt7996_packet_queue *queue,
    const struct npu_wifi_mt7996_packet_queue_config *config) {
  uint32_t entry_index;

  if (queue == NULL || config == NULL || config->entries == NULL ||
      config->acquire == NULL || config->release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (queue->initialized)
    return NPU_RUNTIME_REJECTED;
  if (((uintptr_t)config->entries & (sizeof(uint32_t) - 1U)) != 0U ||
      (config->diagnostic_counters.entries_enqueued != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.entries_enqueued)) ||
      (config->diagnostic_counters.queue_full != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.queue_full)) ||
      (config->diagnostic_counters.fragment_entries_enqueued != NULL &&
       !pointer_is_aligned(
           config->diagnostic_counters.fragment_entries_enqueued)) ||
      config->entry_memory_size < NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT *
                                      NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_SIZE ||
      (uint32_t)config->producer >= NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)config->band >= NPU_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (config->fragment_entries != NULL &&
      (((uintptr_t)config->fragment_entries & (sizeof(uint32_t) - 1U)) != 0U ||
       config->fragment_entry_memory_size <
           NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT *
               NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_SIZE ||
       (uint32_t)config->fragment_producer >=
           NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(queue, 0U, sizeof(*queue));
  queue->entries = config->entries;
  queue->fragment_entries = config->fragment_entries;
  queue->acquire = config->acquire;
  queue->release = config->release;
  queue->lock_context = config->lock_context;
  queue->diagnostic_counters = config->diagnostic_counters;
  queue->producer = config->producer;
  queue->fragment_producer = config->fragment_producer;
  queue->band = config->band;
  for (entry_index = 0U; entry_index < NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT;
       ++entry_index) {
    volatile struct npu_wifi_mt7996_packet_queue_entry *entry =
        &queue->entries[entry_index];

    entry->packet_id = NPU_WIFI_MT7996_PACKET_QUEUE_UNUSED_PACKET;
    entry->flow_value = 0U;
    entry->total_length = 0U;
    entry->fragment_length = 0U;
    entry->route = 0U;
    entry->flags = 0U;
  }
  if (queue->fragment_entries != NULL) {
    for (entry_index = 0U;
         entry_index < NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT;
         ++entry_index) {
      volatile struct npu_wifi_mt7996_fragment_queue_entry *entry =
          &queue->fragment_entries[entry_index];

      entry->packet_id = NPU_WIFI_MT7996_PACKET_QUEUE_UNUSED_PACKET;
      entry->total_length = 0U;
      entry->fragment_length = 0U;
      entry->reserved[0] = 0U;
      entry->reserved[1] = 0U;
      entry->reserved[2] = 0U;
      entry->flags = 0U;
    }
  }
  an7581_dma_memory_barrier();
  queue->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint16_t next_index(uint16_t producer, uint32_t entry_count) {
  uint32_t next = (uint32_t)producer + 1U;

  if (next == entry_count)
    next = 0U;
  return (uint16_t)next;
}

static enum npu_runtime_result
enqueue_complete(struct npu_wifi_mt7996_packet_queue *queue, int16_t packet_id,
                 uint16_t total_length, uint16_t flow_value, uint16_t route,
                 uint8_t flags, uint16_t fragment_length) {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entry;
  enum npu_runtime_result status;

  status = queue->acquire(queue->lock_context);
  if (status != NPU_RUNTIME_SUCCESS) {
    ++queue->lock_failure_count;
    return status;
  }

  entry = &queue->entries[queue->producer];
  an7581_dma_memory_barrier();
  if ((entry->flags & NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED) != 0U) {
    ++queue->full_count;
    increment_counter(queue->diagnostic_counters.queue_full);
    return release_lock(queue, NPU_RUNTIME_FULL);
  }

  entry->packet_id = (uint32_t)(int32_t)packet_id;
  entry->flow_value = flow_value;
  entry->total_length = total_length;
  entry->fragment_length = fragment_length;
  entry->route = (uint8_t)route;
  an7581_dma_memory_barrier();
  entry->flags = flags | NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED;
  queue->producer =
      next_index(queue->producer, NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT);
  ++queue->enqueued_count;
  increment_counter(queue->diagnostic_counters.entries_enqueued);
  an7581_dma_memory_barrier();
  status = release_lock(queue, NPU_RUNTIME_SUCCESS);
  if (status != NPU_RUNTIME_SUCCESS)
    ++queue->lock_failure_count;
  return status;
}

static enum npu_runtime_result
enqueue_fragment(struct npu_wifi_mt7996_packet_queue *queue, int16_t packet_id,
                 uint16_t total_length, uint8_t flags,
                 uint16_t fragment_length) {
  volatile struct npu_wifi_mt7996_fragment_queue_entry *entry;

  if (queue->fragment_entries == NULL ||
      (uint32_t)queue->fragment_producer >=
          NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  entry = &queue->fragment_entries[queue->fragment_producer];
  an7581_dma_memory_barrier();
  if ((entry->flags & NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED) != 0U) {
    ++queue->fragment_full_count;
    return NPU_RUNTIME_FULL;
  }

  entry->packet_id = (uint32_t)(int32_t)packet_id;
  entry->total_length = total_length;
  entry->fragment_length = fragment_length;
  an7581_dma_memory_barrier();
  entry->flags = flags | NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED;
  queue->fragment_producer = next_index(
      queue->fragment_producer, NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT);
  ++queue->fragment_enqueued_count;
  increment_counter(queue->diagnostic_counters.fragment_entries_enqueued);
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_wifi_mt7996_packet_queue_enqueue(void *context, int16_t packet_id,
                                     uint16_t total_length, uint16_t flow_value,
                                     uint16_t route, uint8_t band,
                                     uint8_t flags, uint16_t fragment_length) {
  struct npu_wifi_mt7996_packet_queue *queue = context;
  uint8_t normalized_band;

  if (queue == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!queue->initialized || queue->entries == NULL ||
      (uint32_t)queue->producer >= NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (packet_id < 0 || (uint32_t)band > NPU_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  normalized_band = band == NPU_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT ? 1U : band;
  if (normalized_band != queue->band)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (fragment_length == total_length)
    return enqueue_complete(queue, packet_id, total_length, flow_value, route,
                            flags, fragment_length);
  return enqueue_fragment(queue, packet_id, total_length, flags,
                          fragment_length);
}

enum npu_runtime_result npu_wifi_mt7996_packet_queue_enqueue_completion(
    void *context,
    const struct npu_wifi_mt7996_tx_done_completion *completion) {
  struct npu_wifi_mt7996_packet_queue *queue = context;

  if (queue == NULL || completion == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (completion->packet_id > UINT16_C(0x7fff))
    return NPU_RUNTIME_OUT_OF_RANGE;
  return npu_wifi_mt7996_packet_queue_enqueue(
      queue, (int16_t)completion->packet_id, completion->packet_length, 0U, 0U,
      queue->band, NPU_WIFI_MT7996_PACKET_QUEUE_TX_DONE_FLAGS,
      completion->packet_length);
}

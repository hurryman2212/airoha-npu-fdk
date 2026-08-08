/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_fragment_queue_consumer.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

#define NPU_WIFI_MT7996_FRAGMENT_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_MT7996_FRAGMENT_DEVICE_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_MT7996_FRAGMENT_PAYLOAD_OFFSET UINT32_C(0x80)
#define NPU_WIFI_MT7996_FRAGMENT_END UINT8_C(2)
#define NPU_WIFI_MT7996_FRAGMENT_INDEX_SHIFT UINT32_C(2)
#define NPU_WIFI_MT7996_FRAGMENT_INDEX_MASK UINT8_C(7)
#define NPU_WIFI_MT7996_FRAGMENT_COUNT_SHIFT UINT32_C(5)
#define NPU_WIFI_MT7996_FRAGMENT_COLLECTION_DELAY UINT32_C(10000)
#define NPU_WIFI_MT7996_FRAGMENT_FORWARD_DELAY UINT32_C(8000)

_Static_assert(sizeof(struct npu_wifi_mt7996_fragment_queue_entry) ==
                   NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_SIZE,
               "MT7996 fragment-queue entry layout changed");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return pointer != NULL && ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool operations_are_valid(
    const struct npu_wifi_mt7996_packet_queue_consumer_operations *operations) {
  return operations->forward_packet != NULL &&
         operations->release_packet != NULL;
}

static bool configuration_is_valid(
    const struct npu_wifi_mt7996_fragment_queue_consumer_config *config) {
  uint32_t packet_span;
  uint32_t physical_offset;

  if (config == NULL || config->packet_cached_memory == NULL ||
      !pointer_is_aligned(config->entries, sizeof(uint32_t)) ||
      !pointer_is_aligned(config->packet_cached_memory, sizeof(uint32_t)) ||
      !pointer_is_aligned(config->retry_limit, sizeof(uint16_t)) ||
      !operations_are_valid(&config->operations) || config->delay == NULL ||
      config->packet_id_limit == 0U ||
      config->packet_id_limit > NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_ID_LIMIT ||
      config->entry_memory_size <
          NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT *
              NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_SIZE ||
      config->consumer >= NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT)
    return false;

  packet_span =
      config->packet_id_limit * NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE;
  physical_offset =
      config->packet_physical_base & NPU_WIFI_MT7996_FRAGMENT_ADDRESS_MASK;
  return (physical_offset & (NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE - 1U)) ==
             0U &&
         config->packet_memory_size >= packet_span &&
         packet_span <=
             (NPU_WIFI_MT7996_FRAGMENT_ADDRESS_MASK + 1U) - physical_offset;
}

enum npu_runtime_result npu_wifi_mt7996_fragment_queue_consumer_initialize(
    struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
    const struct npu_wifi_mt7996_fragment_queue_consumer_config *config) {
  if (consumer == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (consumer->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(consumer, 0U, sizeof(*consumer));
  consumer->entries = config->entries;
  consumer->packet_cached_memory = config->packet_cached_memory;
  consumer->operations = config->operations;
  consumer->diagnostic_counters = config->diagnostic_counters;
  consumer->retry_limit = config->retry_limit;
  consumer->delay = config->delay;
  consumer->operation_context = config->operation_context;
  consumer->packet_memory_size = config->packet_memory_size;
  consumer->packet_physical_base = config->packet_physical_base;
  consumer->packet_id_limit = config->packet_id_limit;
  consumer->consumer = config->consumer;
  consumer->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint16_t next_index(uint16_t index) {
  ++index;
  if (index == NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT)
    index = 0U;
  return index;
}

static bool entry_is_occupied(
    const volatile struct npu_wifi_mt7996_fragment_queue_entry *entry) {
  an7581_dma_memory_barrier();
  return (entry->flags & NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED) != 0U;
}

static uint32_t packet_device_address(
    const struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
    uint16_t packet_id) {
  return ((consumer->packet_physical_base +
           (uint32_t)packet_id * NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE) &
          NPU_WIFI_MT7996_FRAGMENT_ADDRESS_MASK) |
         NPU_WIFI_MT7996_FRAGMENT_DEVICE_ALIAS |
         NPU_WIFI_MT7996_FRAGMENT_PAYLOAD_OFFSET;
}

static void
retire_entry(volatile struct npu_wifi_mt7996_fragment_queue_entry *entry) {
  entry->packet_id = NPU_WIFI_MT7996_PACKET_QUEUE_UNUSED_PACKET;
  entry->total_length = 0U;
  entry->fragment_length = 0U;
  an7581_dma_memory_barrier();
  entry->flags = 0U;
  an7581_dma_memory_barrier();
}

static void
remember_failure(struct npu_wifi_mt7996_fragment_queue_consumer_result *result,
                 enum npu_runtime_result status) {
  if (status != NPU_RUNTIME_SUCCESS &&
      result->first_failure == NPU_RUNTIME_SUCCESS)
    result->first_failure = status;
}

static bool collect_sequence(
    struct npu_wifi_mt7996_fragment_queue_consumer *consumer, uint16_t *indices,
    struct npu_wifi_mt7996_fragment_queue_consumer_result *result) {
  uint16_t index = consumer->consumer;

  while (result->collected < NPU_WIFI_MT7996_FRAGMENT_CHAIN_LIMIT) {
    volatile struct npu_wifi_mt7996_fragment_queue_entry *entry =
        &consumer->entries[index];
    uint32_t retry_count = 0U;
    uint32_t sequence_index;
    uint32_t sequence_count;
    uint32_t collected;

    do {
      if (consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragment_entry_poll_attempts;
      if (entry_is_occupied(entry))
        break;
      ++retry_count;
      ++result->missing_entry_retries;
      consumer->delay(consumer->operation_context,
                      NPU_WIFI_MT7996_FRAGMENT_COLLECTION_DELAY);
      if (retry_count > (uint32_t)*consumer->retry_limit) {
        result->collection_timed_out = true;
        if (consumer->diagnostic_counters != NULL)
          ++consumer->diagnostic_counters->fragment_collection_timeouts;
        return false;
      }
    } while (true);

    collected = result->collected + 1U;
    sequence_count =
        (uint32_t)entry->flags >> NPU_WIFI_MT7996_FRAGMENT_COUNT_SHIFT;
    sequence_index =
        ((uint32_t)entry->flags >> NPU_WIFI_MT7996_FRAGMENT_INDEX_SHIFT) &
        NPU_WIFI_MT7996_FRAGMENT_INDEX_MASK;
    if (consumer->diagnostic_counters != NULL)
      ++consumer->diagnostic_counters->fragment_entries_collected;

    if ((entry->flags & NPU_WIFI_MT7996_FRAGMENT_END) != 0U) {
      indices[result->collected] = index;
      result->collected = collected;
      if (consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragment_end_markers;
      if (sequence_count != collected && consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragment_end_count_mismatches;
      if (sequence_index != collected && consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragment_end_index_mismatches;
      if (sequence_count != collected || sequence_index != collected)
        return false;
      if (consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragment_sequences_validated;
      return true;
    }

    if (sequence_count <= collected) {
      if (consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragment_count_overruns;
      return false;
    }
    indices[result->collected] = index;
    result->collected = collected;
    index = next_index(index);
  }

  if (consumer->diagnostic_counters != NULL)
    ++consumer->diagnostic_counters->fragment_collection_timeouts;
  result->collection_timed_out = true;
  return false;
}

static enum npu_runtime_result forward_fragment(
    struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
    const volatile struct npu_wifi_mt7996_fragment_queue_entry *entry) {
  struct npu_wifi_mt7996_packet_delivery delivery;
  const volatile uint32_t *packet_words;
  enum npu_runtime_result status;
  uint32_t attempt;
  uint16_t packet_id = (uint16_t)entry->packet_id;

  packet_words = __builtin_assume_aligned(
      consumer->packet_cached_memory +
          (uint32_t)packet_id * NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE,
      sizeof(uint32_t));
  delivery = (struct npu_wifi_mt7996_packet_delivery){
      .source_device_address = packet_device_address(consumer, packet_id),
      .packet_word = *packet_words,
      .packet_id = packet_id,
      .total_length = entry->total_length,
      .fragment_length = entry->fragment_length,
      .delivery_flags =
          (uint8_t)(entry->flags >> NPU_WIFI_MT7996_FRAGMENT_INDEX_SHIFT),
      .flag_bit_1 = (entry->flags & NPU_WIFI_MT7996_FRAGMENT_END) != 0U,
  };

  for (attempt = 0U; attempt <= (uint32_t)*consumer->retry_limit; ++attempt) {
    status = consumer->operations.forward_packet(consumer->operation_context,
                                                 &delivery);
    if (status == NPU_RUNTIME_SUCCESS) {
      if (consumer->diagnostic_counters != NULL)
        ++consumer->diagnostic_counters->fragments_forwarded;
      return status;
    }
    if (attempt == 0U && consumer->diagnostic_counters != NULL)
      ++consumer->diagnostic_counters->fragment_initial_forward_failures;
    if (attempt != (uint32_t)*consumer->retry_limit)
      consumer->delay(consumer->operation_context,
                      NPU_WIFI_MT7996_FRAGMENT_FORWARD_DELAY);
  }
  return status;
}

static void
retire_sequence(struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
                const uint16_t *indices, bool forward,
                struct npu_wifi_mt7996_fragment_queue_consumer_result *result) {
  uint32_t sequence_index;

  for (sequence_index = 0U; sequence_index < result->collected;
       ++sequence_index) {
    volatile struct npu_wifi_mt7996_fragment_queue_entry *entry =
        &consumer->entries[indices[sequence_index]];
    uint32_t packet_id = entry->packet_id;
    enum npu_runtime_result status;

    if (packet_id >= consumer->packet_id_limit) {
      remember_failure(result, NPU_RUNTIME_OUT_OF_RANGE);
    } else if (forward) {
      status = forward_fragment(consumer, entry);
      if (status == NPU_RUNTIME_SUCCESS) {
        ++result->forwarded;
        ++consumer->forwarded_fragment_count;
      } else {
        ++result->forward_failures;
        ++consumer->forward_failure_count;
        remember_failure(result, status);
      }
    }

    if (packet_id < consumer->packet_id_limit) {
      status = consumer->operations.release_packet(consumer->operation_context,
                                                   (uint16_t)packet_id);
      if (status != NPU_RUNTIME_SUCCESS) {
        ++result->packet_release_failures;
        ++consumer->packet_release_failure_count;
        remember_failure(result, status);
      }
    }
    retire_entry(entry);
    if (consumer->diagnostic_counters != NULL)
      ++consumer->diagnostic_counters->fragment_entries_retired;
  }
  if (result->collected != 0U)
    consumer->consumer = next_index(indices[result->collected - 1U]);
}

enum npu_runtime_result npu_wifi_mt7996_fragment_queue_consume(
    struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
    struct npu_wifi_mt7996_fragment_queue_consumer_result *result) {
  uint16_t indices[NPU_WIFI_MT7996_FRAGMENT_CHAIN_LIMIT];

  if (consumer == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  result->first_failure = NPU_RUNTIME_SUCCESS;
  if (!consumer->initialized || consumer->entries == NULL ||
      consumer->packet_cached_memory == NULL ||
      consumer->consumer >= NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  if (!entry_is_occupied(&consumer->entries[consumer->consumer])) {
    if (consumer->diagnostic_counters != NULL)
      ++consumer->diagnostic_counters->fragment_queue_empty_observations;
    result->stopped_on_empty = true;
    result->next_consumer = consumer->consumer;
    return NPU_RUNTIME_EMPTY;
  }

  result->sequence_valid = collect_sequence(consumer, indices, result);
  if (result->sequence_valid)
    ++consumer->valid_sequence_count;
  else
    ++consumer->invalid_sequence_count;
  retire_sequence(consumer, indices, result->sequence_valid, result);
  result->next_consumer = consumer->consumer;
  result->pending_work =
      entry_is_occupied(&consumer->entries[consumer->consumer]);
  if (result->first_failure != NPU_RUNTIME_SUCCESS)
    return result->first_failure;
  return result->sequence_valid ? NPU_RUNTIME_SUCCESS : NPU_RUNTIME_REJECTED;
}

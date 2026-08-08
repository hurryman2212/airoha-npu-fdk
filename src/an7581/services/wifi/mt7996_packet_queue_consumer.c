/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_packet_queue_consumer.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

#define NPU_WIFI_MT7996_PACKET_QUEUE_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_MT7996_PACKET_QUEUE_DEVICE_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_MT7996_PACKET_QUEUE_FLAG_BIT_1 UINT8_C(2)
#define NPU_WIFI_MT7996_PACKET_QUEUE_DELIVERY_FLAGS_SHIFT UINT32_C(2)

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool operations_are_valid(
    const struct npu_wifi_mt7996_packet_queue_consumer_operations *operations) {
  return operations->forward_packet != NULL &&
         operations->release_packet != NULL;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

static bool diagnostic_counters_are_valid(
    const struct npu_wifi_mt7996_packet_queue_consumer_diagnostic_counters
        *counters) {
  const volatile uint32_t *values[] = {
      counters->entries_retired,    counters->consume_attempts,
      counters->invalid_packet_ids, counters->zero_lengths,
      counters->packets_forwarded,  counters->forward_failures,
  };
  size_t index;

  for (index = 0U; index < sizeof(values) / sizeof(values[0]); ++index) {
    if (values[index] != NULL &&
        !pointer_is_aligned(values[index], sizeof(uint32_t)))
      return false;
  }
  return true;
}

static bool configuration_is_valid(
    const struct npu_wifi_mt7996_packet_queue_consumer_config *config) {
  uint32_t packet_span;
  uint32_t physical_offset;

  if (config == NULL || config->entries == NULL ||
      config->packet_cached_memory == NULL ||
      !operations_are_valid(&config->operations) ||
      !diagnostic_counters_are_valid(&config->diagnostic_counters) ||
      config->packet_id_limit == 0U ||
      config->packet_id_limit > NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_ID_LIMIT ||
      (uint32_t)config->consumer >= NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      !pointer_is_aligned(config->entries, sizeof(uint32_t)) ||
      !pointer_is_aligned(config->packet_cached_memory, sizeof(uint32_t)) ||
      config->entry_memory_size < NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT *
                                      NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_SIZE)
    return false;

  packet_span =
      config->packet_id_limit * NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE;
  physical_offset =
      config->packet_physical_base & NPU_WIFI_MT7996_PACKET_QUEUE_ADDRESS_MASK;
  return (physical_offset & (NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE - 1U)) ==
             0U &&
         config->packet_memory_size >= packet_span &&
         packet_span <=
             (NPU_WIFI_MT7996_PACKET_QUEUE_ADDRESS_MASK + 1U) - physical_offset;
}

enum npu_runtime_result npu_wifi_mt7996_packet_queue_consumer_initialize(
    struct npu_wifi_mt7996_packet_queue_consumer *consumer,
    const struct npu_wifi_mt7996_packet_queue_consumer_config *config) {
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
  consumer->operation_context = config->operation_context;
  consumer->packet_memory_size = config->packet_memory_size;
  consumer->packet_physical_base = config->packet_physical_base;
  consumer->packet_id_limit = config->packet_id_limit;
  consumer->consumer = config->consumer;
  consumer->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t packet_device_address(
    const struct npu_wifi_mt7996_packet_queue_consumer *consumer,
    uint16_t packet_id) {
  return ((consumer->packet_physical_base +
           (uint32_t)packet_id * NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE) &
          NPU_WIFI_MT7996_PACKET_QUEUE_ADDRESS_MASK) |
         NPU_WIFI_MT7996_PACKET_QUEUE_DEVICE_ALIAS |
         NPU_WIFI_MT7996_PACKET_QUEUE_PAYLOAD_OFFSET;
}

static uint16_t next_consumer_index(uint16_t consumer) {
  uint32_t next = (uint32_t)consumer + 1U;

  if (next == NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT)
    next = 0U;
  return (uint16_t)next;
}

static void
retire_entry(volatile struct npu_wifi_mt7996_packet_queue_entry *entry) {
  entry->packet_id = NPU_WIFI_MT7996_PACKET_QUEUE_UNUSED_PACKET;
  entry->flow_value = 0U;
  entry->total_length = 0U;
  entry->fragment_length = 0U;
  entry->route = 0U;
  an7581_dma_memory_barrier();
  entry->flags = 0U;
  an7581_dma_memory_barrier();
}

static void
remember_failure(struct npu_wifi_mt7996_packet_queue_consumer_result *result,
                 enum npu_runtime_result status) {
  if (result->first_failure == NPU_RUNTIME_SUCCESS)
    result->first_failure = status;
}

static void process_valid_entry(
    struct npu_wifi_mt7996_packet_queue_consumer *consumer,
    const volatile struct npu_wifi_mt7996_packet_queue_entry *entry,
    struct npu_wifi_mt7996_packet_queue_consumer_result *result) {
  struct npu_wifi_mt7996_packet_delivery delivery;
  const volatile uint32_t *packet_words;
  enum npu_runtime_result status;
  uint32_t packet_offset =
      entry->packet_id * NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE;
  uint16_t packet_id = (uint16_t)entry->packet_id;

  packet_words = __builtin_assume_aligned(
      consumer->packet_cached_memory + packet_offset, sizeof(uint32_t));
  delivery = (struct npu_wifi_mt7996_packet_delivery){
      .source_device_address = packet_device_address(consumer, packet_id),
      .packet_word = *packet_words,
      .packet_id = packet_id,
      .total_length = entry->total_length,
      .flow_value = entry->flow_value,
      .fragment_length = entry->fragment_length,
      .delivery_flags =
          (uint8_t)(entry->flags >>
                    NPU_WIFI_MT7996_PACKET_QUEUE_DELIVERY_FLAGS_SHIFT),
      .route = entry->route,
      .flag_bit_1 =
          (entry->flags & NPU_WIFI_MT7996_PACKET_QUEUE_FLAG_BIT_1) != 0U,
  };
  status = consumer->operations.forward_packet(consumer->operation_context,
                                               &delivery);
  if (status == NPU_RUNTIME_SUCCESS) {
    ++result->forwarded;
    ++consumer->forwarded_count;
    increment_counter(consumer->diagnostic_counters.packets_forwarded);
  } else {
    ++result->forward_failures;
    ++consumer->forward_failure_count;
    increment_counter(consumer->diagnostic_counters.forward_failures);
    remember_failure(result, status);
  }

  status = consumer->operations.release_packet(consumer->operation_context,
                                               packet_id);
  if (status != NPU_RUNTIME_SUCCESS) {
    ++result->packet_release_failures;
    ++consumer->packet_release_failure_count;
    remember_failure(result, status);
  }
}

enum npu_runtime_result npu_wifi_mt7996_packet_queue_consume(
    struct npu_wifi_mt7996_packet_queue_consumer *consumer, uint32_t budget,
    struct npu_wifi_mt7996_packet_queue_consumer_result *result) {
  if (consumer == NULL || result == NULL || budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  result->first_failure = NPU_RUNTIME_SUCCESS;
  if (!consumer->initialized || consumer->entries == NULL ||
      (uint32_t)consumer->consumer >= NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (budget > NPU_WIFI_MT7996_PACKET_QUEUE_PROCESS_LIMIT)
    budget = NPU_WIFI_MT7996_PACKET_QUEUE_PROCESS_LIMIT;

  while (result->processed < budget) {
    volatile struct npu_wifi_mt7996_packet_queue_entry *entry =
        &consumer->entries[consumer->consumer];

    an7581_dma_memory_barrier();
    if ((entry->flags & NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED) == 0U) {
      result->stopped_on_empty = true;
      break;
    }

    increment_counter(consumer->diagnostic_counters.consume_attempts);

    if (entry->packet_id >= consumer->packet_id_limit) {
      ++result->invalid_packets;
      ++consumer->invalid_packet_count;
      increment_counter(consumer->diagnostic_counters.invalid_packet_ids);
    } else if (entry->total_length == 0U) {
      ++result->zero_lengths;
      ++consumer->zero_length_count;
      increment_counter(consumer->diagnostic_counters.zero_lengths);
    } else {
      process_valid_entry(consumer, entry, result);
    }

    retire_entry(entry);
    consumer->consumer = next_consumer_index(consumer->consumer);
    ++result->processed;
    ++consumer->processed_count;
    increment_counter(consumer->diagnostic_counters.entries_retired);
  }

  result->next_consumer = consumer->consumer;
  return result->processed == 0U ? NPU_RUNTIME_EMPTY : NPU_RUNTIME_SUCCESS;
}

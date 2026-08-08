/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_cpu_queue.h"

#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct npu_wifi_rro_cpu_queue_entry) == 16U,
               "Wi-Fi RRO CPU queue entry layout changed");

enum npu_runtime_result npu_wifi_rro_cpu_queue_initialize(
    struct npu_wifi_rro_cpu_queue *queue, volatile void *entry_memory,
    size_t entry_memory_size,
    const struct npu_wifi_rro_cpu_queue_diagnostic_counters
        *diagnostic_counters,
    uint16_t producer, uint16_t consumer) {
  const uint16_t entry_count = (uint16_t)NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT;
  size_t required_size =
      (size_t)entry_count * sizeof(struct npu_wifi_rro_cpu_queue_entry);

  if (queue == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (entry_memory == NULL ||
      ((uintptr_t)entry_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      entry_count == 0U ||
      (uint32_t)entry_count > NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT ||
      entry_memory_size < required_size || producer >= entry_count ||
      consumer >= entry_count ||
      (diagnostic_counters != NULL &&
       ((diagnostic_counters->entries_enqueued != NULL &&
         ((uintptr_t)diagnostic_counters->entries_enqueued &
          (sizeof(uint32_t) - 1U)) != 0U) ||
        (diagnostic_counters->full_waits != NULL &&
         ((uintptr_t)diagnostic_counters->full_waits &
          (sizeof(uint32_t) - 1U)) != 0U) ||
        (diagnostic_counters->entries_processed != NULL &&
         ((uintptr_t)diagnostic_counters->entries_processed &
          (sizeof(uint32_t) - 1U)) != 0U) ||
        (diagnostic_counters->normal_entries != NULL &&
         ((uintptr_t)diagnostic_counters->normal_entries &
          (sizeof(uint32_t) - 1U)) != 0U))))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(queue, 0U, sizeof(*queue));
  queue->entries = entry_memory;
  if (diagnostic_counters != NULL)
    queue->diagnostic_counters = *diagnostic_counters;
  queue->producer = producer;
  queue->consumer = consumer;
  queue->entry_count = entry_count;
  return NPU_RUNTIME_SUCCESS;
}

static bool
operations_valid(const struct npu_wifi_rro_cpu_queue_operations *operations) {
  return operations != NULL && operations->dispatch != NULL &&
         operations->dispatch_special != NULL && operations->release != NULL;
}

static bool pending_entry_matches(
    const struct npu_wifi_rro_cpu_queue *queue,
    const volatile struct npu_wifi_rro_cpu_queue_entry *entry) {
  return entry->type == queue->pending.type &&
         entry->buffer_id == queue->pending.buffer_word &&
         entry->packet_control == queue->pending.packet_control;
}

static void
stage_entry(struct npu_wifi_rro_cpu_queue *queue,
            const volatile struct npu_wifi_rro_cpu_queue_entry *entry) {
  queue->pending.type = entry->type;
  __asm__ volatile("" ::: "memory");
  queue->pending.buffer_word = entry->buffer_id;
  queue->pending.packet_control = entry->packet_control;
  queue->pending.special = (queue->pending.packet_control &
                            NPU_WIFI_RRO_CPU_QUEUE_SPECIAL_BIT) != 0U;
  queue->pending.action = queue->pending.special
                              ? NPU_WIFI_RRO_CPU_QUEUE_DISPATCH_SPECIAL
                              : NPU_WIFI_RRO_CPU_QUEUE_DISPATCH_NORMAL;
  if (queue->diagnostic_counters.entries_processed != NULL)
    ++*queue->diagnostic_counters.entries_processed;
  if (!queue->pending.special &&
      queue->diagnostic_counters.normal_entries != NULL)
    ++*queue->diagnostic_counters.normal_entries;
  queue->pending_valid = true;
}

static uint16_t packet_length(uint32_t packet_control) {
  return (uint16_t)((packet_control >> 3U) & UINT32_C(0x3fff));
}

static uint16_t next_queue_index(uint16_t index, uint16_t entry_count) {
  uint16_t next = (uint16_t)(index + 1U);

  return next == entry_count ? 0U : next;
}

static enum npu_runtime_result
dispatch_pending(struct npu_wifi_rro_cpu_queue *queue,
                 const struct npu_wifi_rro_cpu_queue_operations *operations,
                 void *context, struct npu_wifi_rro_cpu_queue_result *result) {
  uint16_t buffer_id = (uint16_t)queue->pending.buffer_word;
  uint16_t length = packet_length(queue->pending.packet_control);
  enum npu_runtime_result status;

  if (queue->pending.action == NPU_WIFI_RRO_CPU_QUEUE_DISPATCH_NORMAL) {
    status =
        operations->dispatch(context, (int16_t)buffer_id, length, 2U, length);
  } else if (queue->pending.action == NPU_WIFI_RRO_CPU_QUEUE_DISPATCH_SPECIAL) {
    uint16_t data_offset =
        (uint16_t)((queue->pending.packet_control >> 16U) &
                   NPU_WIFI_RRO_CPU_QUEUE_SPECIAL_OFFSET_MASK);
    uint16_t payload_length = (uint16_t)(length - data_offset);

    status = operations->dispatch_special(context, buffer_id, payload_length,
                                          data_offset);
  } else {
    status = operations->release(context, buffer_id);
  }

  if (status == NPU_RUNTIME_REJECTED &&
      queue->pending.action != NPU_WIFI_RRO_CPU_QUEUE_RELEASE) {
    queue->pending.action = NPU_WIFI_RRO_CPU_QUEUE_RELEASE;
    ++queue->rejected_count;
    ++result->rejected_count;
    return NPU_RUNTIME_REJECTED;
  }
  return status;
}

static void commit_pending(struct npu_wifi_rro_cpu_queue *queue,
                           struct npu_wifi_rro_cpu_queue_result *result) {
  volatile struct npu_wifi_rro_cpu_queue_entry *entry =
      &queue->entries[queue->consumer];

  __asm__ volatile("" ::: "memory");
  entry->type = NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER;
  queue->consumer = next_queue_index(queue->consumer, queue->entry_count);
  queue->pending_valid = false;
  ++queue->dequeued_count;
  ++result->consumed_count;
  if (queue->pending.special) {
    ++queue->special_count;
    ++result->special_count;
  } else {
    ++queue->normal_count;
    ++result->normal_count;
  }
}

static void commit_invalid_entry(struct npu_wifi_rro_cpu_queue *queue,
                                 struct npu_wifi_rro_cpu_queue_result *result) {
  volatile struct npu_wifi_rro_cpu_queue_entry *entry =
      &queue->entries[queue->consumer];

  entry->type = NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER;
  queue->consumer = next_queue_index(queue->consumer, queue->entry_count);
  ++queue->invalid_type_count;
  ++result->invalid_type_count;
  ++result->consumed_count;
}

enum npu_runtime_result npu_wifi_rro_cpu_queue_consume(
    struct npu_wifi_rro_cpu_queue *queue, uint32_t consume_budget,
    const struct npu_wifi_rro_cpu_queue_operations *operations, void *context,
    struct npu_wifi_rro_cpu_queue_result *result) {
  enum npu_runtime_result status;

  if (queue == NULL || !operations_valid(operations) || result == NULL ||
      consume_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (queue->entries == NULL || queue->entry_count == 0U ||
      (uint32_t)queue->entry_count > NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT ||
      queue->consumer >= queue->entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  while (result->consumed_count < consume_budget) {
    volatile struct npu_wifi_rro_cpu_queue_entry *entry =
        &queue->entries[queue->consumer];

    if (queue->pending_valid) {
      if (!pending_entry_matches(queue, entry))
        return NPU_RUNTIME_OWNERSHIP_ERROR;
    } else {
      uint32_t type = entry->type;

      if (type == NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER) {
        result->empty = true;
        result->consumer = queue->consumer;
        return result->consumed_count == 0U ? NPU_RUNTIME_EMPTY
                                            : NPU_RUNTIME_SUCCESS;
      }
      if ((type & NPU_WIFI_RRO_CPU_QUEUE_PACKET_TYPE_MASK) !=
          NPU_WIFI_RRO_CPU_QUEUE_PACKET_TYPE) {
        commit_invalid_entry(queue, result);
        continue;
      }
      stage_entry(queue, entry);
    }

    status = dispatch_pending(queue, operations, context, result);
    if (status == NPU_RUNTIME_REJECTED)
      continue;
    if (status != NPU_RUNTIME_SUCCESS) {
      result->consumer = queue->consumer;
      return status;
    }
    commit_pending(queue, result);
  }

  result->consumer = queue->consumer;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_wifi_rro_cpu_queue_enqueue(void *context, uint16_t buffer_id,
                               uint32_t packet_control) {
  struct npu_wifi_rro_cpu_queue *queue = context;
  volatile struct npu_wifi_rro_cpu_queue_entry *entry;
  volatile uint32_t iteration;

  if (queue == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (queue->entries == NULL || queue->entry_count == 0U ||
      (uint32_t)queue->entry_count > NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT ||
      queue->producer >= queue->entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  entry = &queue->entries[queue->producer];
  while (entry->type != NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER) {
    if (queue->diagnostic_counters.full_waits != NULL)
      ++*queue->diagnostic_counters.full_waits;
    /* Match the producer-side queue wait recovered from the vendor worker. */
    for (iteration = 0U; iteration < 100U; ++iteration)
      __asm__ volatile("" ::: "memory");
  }

  entry->buffer_id = buffer_id;
  entry->packet_control = packet_control;
  __asm__ volatile("" ::: "memory");
  entry->type = NPU_WIFI_RRO_CPU_QUEUE_PACKET_TYPE;
  if (queue->diagnostic_counters.entries_enqueued != NULL)
    ++*queue->diagnostic_counters.entries_enqueued;

  queue->producer = next_queue_index(queue->producer, queue->entry_count);
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_tx_done.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

#define NPU_WIFI_MT7996_TX_DONE_OWNED_BY_NPU (UINT32_C(1) << 31)
#define NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_CONTROL UINT32_C(0x07000100)
#define NPU_WIFI_MT7996_TX_DONE_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_MT7996_TX_DONE_DEVICE_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_MT7996_TX_DONE_RECORD_TYPE_SHIFT UINT32_C(27)
#define NPU_WIFI_MT7996_TX_DONE_RECORD_TYPE_6 UINT32_C(6)
#define NPU_WIFI_MT7996_TX_DONE_RECORD_TYPE_24 UINT32_C(24)
#define NPU_WIFI_MT7996_TX_DONE_TOKEN_COUNT_SHIFT UINT32_C(16)
#define NPU_WIFI_MT7996_TX_DONE_TOKEN_COUNT_MASK UINT32_C(0xff)
#define NPU_WIFI_MT7996_TX_DONE_RECORD_LENGTH_MASK UINT32_C(0xffff)
#define NPU_WIFI_MT7996_TX_DONE_TOKEN_WORD_SKIP_MASK UINT32_C(0xc0000000)
#define NPU_WIFI_MT7996_TX_DONE_TOKEN_MASK UINT32_C(0x7fff)
#define NPU_WIFI_MT7996_TX_DONE_SECOND_TOKEN_SHIFT UINT32_C(15)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_LENGTH_MASK UINT32_C(0x3fff)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_LENGTH_SHIFT UINT32_C(16)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_MARKER UINT32_C(0x08000000)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_MARKER_SHIFT UINT32_C(3)

_Static_assert(sizeof(struct npu_wifi_mt7996_tx_done_descriptor) ==
                   NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_SIZE,
               "MT7996 TX-done descriptor layout changed");
_Static_assert(sizeof(struct npu_wifi_tx_ring_registers) == 16U,
               "MT7996 TX-done register layout changed");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

static bool operations_are_valid(
    const struct npu_wifi_mt7996_tx_done_operations *operations) {
  return operations->allocate_packet != NULL &&
         operations->release_packet != NULL &&
         operations->release_token != NULL &&
         operations->enqueue_packet != NULL &&
         operations->discard_cache_line != NULL;
}

static bool
configuration_is_valid(const struct npu_wifi_mt7996_tx_done_config *config) {
  uint32_t packet_span;
  uint32_t physical_offset;

  if (config == NULL || config->descriptors == NULL ||
      config->packet_ids == NULL || config->packet_device_memory == NULL ||
      config->packet_cached_memory == NULL || config->registers == NULL ||
      !operations_are_valid(&config->operations) || config->ring_count == 0U ||
      config->ring_count > NPU_WIFI_MT7996_TX_DONE_RING_LIMIT ||
      config->packet_id_limit == 0U ||
      config->packet_id_limit > NPU_WIFI_MT7996_TX_DONE_PACKET_ID_LIMIT ||
      config->active_token_count == 0U ||
      config->active_token_count > NPU_WIFI_MT7996_TX_DONE_TOKEN_ID_LIMIT)
    return false;
  if (!pointer_is_aligned(config->descriptors, sizeof(uint32_t)) ||
      !pointer_is_aligned(config->packet_ids, sizeof(uint16_t)) ||
      !pointer_is_aligned(config->packet_device_memory,
                          NPU_WIFI_MT7996_TX_DONE_CACHE_LINE_SIZE) ||
      !pointer_is_aligned(config->packet_cached_memory,
                          NPU_WIFI_MT7996_TX_DONE_CACHE_LINE_SIZE) ||
      !pointer_is_aligned(config->registers, sizeof(uint32_t)))
    return false;
  if ((config->records_processed_counter != NULL &&
       !pointer_is_aligned(config->records_processed_counter,
                           sizeof(uint32_t))) ||
      (config->diagnostic_counters != NULL &&
       !pointer_is_aligned(config->diagnostic_counters, sizeof(uint32_t))) ||
      (config->invalid_record_type_counter != NULL &&
       !pointer_is_aligned(config->invalid_record_type_counter,
                           sizeof(uint32_t))))
    return false;
  if (config->descriptor_memory_size <
          config->ring_count * NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_SIZE ||
      config->packet_id_memory_size <
          config->ring_count * sizeof(*config->packet_ids))
    return false;

  packet_span = config->packet_id_limit * NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE;
  physical_offset =
      config->packet_physical_base & NPU_WIFI_MT7996_TX_DONE_ADDRESS_MASK;
  return (physical_offset & (NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE - 1U)) == 0U &&
         config->packet_memory_size >= packet_span &&
         packet_span <=
             (NPU_WIFI_MT7996_TX_DONE_ADDRESS_MASK + 1U) - physical_offset;
}

static uint32_t
packet_device_address(const struct npu_wifi_mt7996_tx_done *tx_done,
                      uint16_t packet_id, uint32_t offset) {
  return (((tx_done->packet_physical_base +
            (uint32_t)packet_id * NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE) &
           NPU_WIFI_MT7996_TX_DONE_ADDRESS_MASK) |
          NPU_WIFI_MT7996_TX_DONE_DEVICE_ALIAS) +
         offset;
}

enum npu_runtime_result npu_wifi_mt7996_tx_done_initialize(
    struct npu_wifi_mt7996_tx_done *tx_done,
    const struct npu_wifi_mt7996_tx_done_config *config) {
  uint32_t descriptor_index;

  if (tx_done == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (tx_done->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(tx_done, 0U, sizeof(*tx_done));
  tx_done->descriptors = config->descriptors;
  tx_done->packet_ids = config->packet_ids;
  tx_done->packet_device_memory = config->packet_device_memory;
  tx_done->packet_cached_memory = config->packet_cached_memory;
  tx_done->registers = config->registers;
  tx_done->operations = config->operations;
  tx_done->operation_context = config->operation_context;
  tx_done->diagnostic_counters = config->diagnostic_counters;
  tx_done->records_processed_counter = config->records_processed_counter;
  tx_done->invalid_record_type_counter = config->invalid_record_type_counter;
  tx_done->packet_memory_size = config->packet_memory_size;
  tx_done->packet_physical_base = config->packet_physical_base;
  tx_done->ring_count = config->ring_count;
  tx_done->packet_id_limit = config->packet_id_limit;
  tx_done->active_token_count = config->active_token_count;

  for (descriptor_index = 0U; descriptor_index < tx_done->ring_count;
       ++descriptor_index) {
    const uint16_t packet_id = tx_done->packet_ids[descriptor_index];
    const volatile struct npu_wifi_mt7996_tx_done_descriptor *descriptor =
        &tx_done->descriptors[descriptor_index];

    if ((uint32_t)packet_id >= tx_done->packet_id_limit ||
        descriptor->buffer_address !=
            packet_device_address(tx_done, packet_id,
                                  NPU_WIFI_MT7996_TX_DONE_RECORD_OFFSET) ||
        descriptor->control != NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_CONTROL)
      return NPU_RUNTIME_OUT_OF_RANGE;
  }

  an7581_dma_memory_barrier();
  tx_done->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t discard_cache_range(struct npu_wifi_mt7996_tx_done *tx_done,
                                    uint32_t record_address, uint32_t length) {
  uint32_t cache_address =
      record_address & ~(NPU_WIFI_MT7996_TX_DONE_CACHE_LINE_SIZE - 1U);
  uint32_t last_address = (record_address + length - 1U) &
                          ~(NPU_WIFI_MT7996_TX_DONE_CACHE_LINE_SIZE - 1U);
  uint32_t discarded = 0U;

  do {
    tx_done->operations.discard_cache_line(tx_done->operation_context,
                                           cache_address);
    cache_address += NPU_WIFI_MT7996_TX_DONE_CACHE_LINE_SIZE;
    ++discarded;
  } while (cache_address <= last_address);
  an7581_dma_memory_barrier();
  return discarded;
}

static void release_token(struct npu_wifi_mt7996_tx_done *tx_done,
                          uint32_t token,
                          struct npu_wifi_mt7996_tx_done_result *result) {
  enum npu_runtime_result status;

  if (token == NPU_WIFI_MT7996_TX_DONE_TOKEN_SENTINEL)
    return;

  ++result->tokens_seen;
  if (token >= tx_done->active_token_count) {
    ++result->invalid_tokens;
    ++tx_done->invalid_token_count;
    if (tx_done->diagnostic_counters != NULL)
      ++tx_done->diagnostic_counters->invalid_completion_token_ids;
    return;
  }

  if (tx_done->diagnostic_counters != NULL)
    ++tx_done->diagnostic_counters->completion_token_release_attempts;
  status = tx_done->operations.release_token(tx_done->operation_context,
                                             (uint16_t)token);
  if (status == NPU_RUNTIME_SUCCESS) {
    ++result->tokens_released;
    ++tx_done->token_release_count;
  } else {
    result->stop_status = status;
    ++result->token_release_failures;
    ++tx_done->token_release_failure_count;
  }
}

static void parse_token_record(struct npu_wifi_mt7996_tx_done *tx_done,
                               const volatile uint32_t *record_words,
                               uint32_t record_length, uint32_t expected_tokens,
                               struct npu_wifi_mt7996_tx_done_result *result) {
  uint32_t payload_bytes =
      record_length - NPU_WIFI_MT7996_TX_DONE_RECORD_HEADER_SIZE;
  uint32_t payload_index = 0U;
  uint32_t initial_tokens_seen = result->tokens_seen;

  while (result->tokens_seen - initial_tokens_seen < expected_tokens &&
         payload_bytes != 0U) {
    uint32_t token_word = record_words[3U + payload_index];

    if ((token_word & NPU_WIFI_MT7996_TX_DONE_TOKEN_WORD_SKIP_MASK) == 0U) {
      release_token(tx_done, token_word & NPU_WIFI_MT7996_TX_DONE_TOKEN_MASK,
                    result);
      release_token(tx_done,
                    (token_word >> NPU_WIFI_MT7996_TX_DONE_SECOND_TOKEN_SHIFT) &
                        NPU_WIFI_MT7996_TX_DONE_TOKEN_MASK,
                    result);
    }
    payload_bytes -= sizeof(uint32_t);
    ++payload_index;
  }

  if (result->tokens_seen - initial_tokens_seen < expected_tokens) {
    uint32_t missing =
        expected_tokens - (result->tokens_seen - initial_tokens_seen);

    result->missing_tokens += missing;
    tx_done->missing_token_count += missing;
  }
}

static bool record_layout_is_valid(uint32_t record_length) {
  return record_length >= NPU_WIFI_MT7996_TX_DONE_RECORD_HEADER_SIZE &&
         record_length <= NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE -
                              NPU_WIFI_MT7996_TX_DONE_RECORD_OFFSET &&
         (record_length & (sizeof(uint32_t) - 1U)) == 0U;
}

static uint32_t next_ring_index(uint32_t index, uint32_t ring_count) {
  ++index;
  return index == ring_count ? 0U : index;
}

enum npu_runtime_result
npu_wifi_mt7996_tx_done_process(struct npu_wifi_mt7996_tx_done *tx_done,
                                uint32_t budget,
                                struct npu_wifi_mt7996_tx_done_result *result) {
  enum npu_runtime_result allocation_status = NPU_RUNTIME_SUCCESS;

  if (tx_done == NULL || result == NULL || budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  result->stop_status = NPU_RUNTIME_SUCCESS;
  if (!tx_done->initialized || tx_done->consumer >= tx_done->ring_count ||
      tx_done->publish_batch >= NPU_WIFI_MT7996_TX_DONE_PUBLISH_GRANULARITY)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (budget > NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT)
    budget = NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT;

  while (result->processed < budget) {
    volatile struct npu_wifi_mt7996_tx_done_descriptor *descriptor =
        &tx_done->descriptors[tx_done->consumer];
    struct npu_wifi_mt7996_tx_done_completion completion;
    const volatile uint32_t *record_words;
    uint32_t consumed_index;
    uint32_t descriptor_control;
    uint32_t expected_tokens;
    uint32_t packet_offset;
    uint32_t record_address;
    uint32_t record_header;
    uint32_t record_length;
    uint32_t record_type;
    volatile uint32_t *packet_control;
    uint16_t old_packet_id;
    uint16_t replacement_packet_id;

    an7581_dma_memory_barrier();
    descriptor_control = descriptor->control;
    if ((descriptor_control & NPU_WIFI_MT7996_TX_DONE_OWNED_BY_NPU) == 0U) {
      result->stopped_on_hardware_owned = true;
      break;
    }

    allocation_status = tx_done->operations.allocate_packet(
        tx_done->operation_context, &replacement_packet_id);
    if (allocation_status != NPU_RUNTIME_SUCCESS) {
      ++tx_done->allocation_failure_count;
      result->stop_status = allocation_status;
      result->stopped_on_allocation_failure = true;
      break;
    }
    if ((uint32_t)replacement_packet_id >= tx_done->packet_id_limit) {
      (void)tx_done->operations.release_packet(tx_done->operation_context,
                                               replacement_packet_id);
      ++tx_done->allocation_failure_count;
      allocation_status = NPU_RUNTIME_OUT_OF_RANGE;
      result->stop_status = NPU_RUNTIME_OUT_OF_RANGE;
      result->stopped_on_allocation_failure = true;
      break;
    }
    increment_counter(tx_done->records_processed_counter);

    consumed_index = tx_done->consumer;
    old_packet_id = tx_done->packet_ids[consumed_index];
    if ((uint32_t)old_packet_id >= tx_done->packet_id_limit) {
      (void)tx_done->operations.release_packet(tx_done->operation_context,
                                               replacement_packet_id);
      result->stop_status = NPU_RUNTIME_OUT_OF_RANGE;
      return NPU_RUNTIME_OUT_OF_RANGE;
    }

    packet_offset =
        (uint32_t)old_packet_id * NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE;
    record_words =
        __builtin_assume_aligned(tx_done->packet_device_memory + packet_offset +
                                     NPU_WIFI_MT7996_TX_DONE_RECORD_OFFSET,
                                 sizeof(uint32_t));
    record_address = packet_device_address(
        tx_done, old_packet_id, NPU_WIFI_MT7996_TX_DONE_RECORD_OFFSET);
    record_header = record_words[0];
    record_type = record_header >> NPU_WIFI_MT7996_TX_DONE_RECORD_TYPE_SHIFT;
    record_length = record_header & NPU_WIFI_MT7996_TX_DONE_RECORD_LENGTH_MASK;
    expected_tokens =
        (record_header >> NPU_WIFI_MT7996_TX_DONE_TOKEN_COUNT_SHIFT) &
        NPU_WIFI_MT7996_TX_DONE_TOKEN_COUNT_MASK;

    if (record_type != NPU_WIFI_MT7996_TX_DONE_RECORD_TYPE_6 &&
        record_type != NPU_WIFI_MT7996_TX_DONE_RECORD_TYPE_24) {
      increment_counter(tx_done->invalid_record_type_counter);
      ++result->invalid_records;
      ++tx_done->invalid_record_count;
      result->cache_lines_discarded += discard_cache_range(
          tx_done, record_address, NPU_WIFI_MT7996_TX_DONE_RECORD_HEADER_SIZE);
    } else if (!record_layout_is_valid(record_length)) {
      ++result->invalid_records;
      ++tx_done->invalid_record_count;
      result->cache_lines_discarded += discard_cache_range(
          tx_done, record_address, NPU_WIFI_MT7996_TX_DONE_RECORD_HEADER_SIZE);
    } else {
      parse_token_record(tx_done, record_words, record_length, expected_tokens,
                         result);
      result->cache_lines_discarded +=
          discard_cache_range(tx_done, record_address, record_length);
    }

    tx_done->packet_ids[consumed_index] = replacement_packet_id;
    completion.packet_id = old_packet_id;
    completion.packet_length =
        (uint16_t)((descriptor_control >>
                    NPU_WIFI_MT7996_TX_DONE_PACKET_LENGTH_SHIFT) &
                   NPU_WIFI_MT7996_TX_DONE_PACKET_LENGTH_MASK);
    packet_control = __builtin_assume_aligned(
        tx_done->packet_cached_memory + packet_offset, sizeof(uint32_t));
    *packet_control = NPU_WIFI_MT7996_TX_DONE_PACKET_MARKER |
                      ((uint32_t)completion.packet_length
                       << NPU_WIFI_MT7996_TX_DONE_PACKET_MARKER_SHIFT);
    if (tx_done->operations.enqueue_packet(
            tx_done->operation_context, &completion) != NPU_RUNTIME_SUCCESS) {
      enum npu_runtime_result release_status =
          tx_done->operations.release_packet(tx_done->operation_context,
                                             old_packet_id);

      result->enqueue_failed = true;
      ++tx_done->enqueue_failure_count;
      if (release_status != NPU_RUNTIME_SUCCESS) {
        ++result->packet_release_failures;
        ++tx_done->packet_release_failure_count;
        result->stop_status = release_status;
      }
    }

    descriptor->buffer_address = packet_device_address(
        tx_done, replacement_packet_id, NPU_WIFI_MT7996_TX_DONE_RECORD_OFFSET);
    an7581_dma_memory_barrier();
    descriptor->control = NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_CONTROL;
    tx_done->consumer = next_ring_index(consumed_index, tx_done->ring_count);
    ++tx_done->publish_batch;
    if ((uint32_t)tx_done->publish_batch ==
        NPU_WIFI_MT7996_TX_DONE_PUBLISH_GRANULARITY) {
      an7581_dma_memory_barrier();
      tx_done->registers->cpu_index = consumed_index;
      tx_done->publish_batch = 0U;
      ++result->consumer_publishes;
    }

    result->last_consumed = consumed_index;
    ++result->processed;
    ++tx_done->processed_count;
  }

  result->next_consumer = tx_done->consumer;
  result->next_publish_batch = tx_done->publish_batch;
  if (result->processed != 0U)
    return NPU_RUNTIME_SUCCESS;
  if (result->stopped_on_allocation_failure)
    return allocation_status;
  return NPU_RUNTIME_EMPTY;
}

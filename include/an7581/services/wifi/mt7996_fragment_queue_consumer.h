/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_FRAGMENT_QUEUE_CONSUMER_H
#define NPU_WIFI_MT7996_FRAGMENT_QUEUE_CONSUMER_H

#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/mt7996_packet_queue_consumer.h"

#define NPU_WIFI_MT7996_FRAGMENT_CHAIN_LIMIT UINT32_C(7)

typedef void (*npu_wifi_mt7996_fragment_queue_delay)(void *context,
                                                     uint32_t iterations);

struct npu_wifi_mt7996_fragment_queue_consumer_config {
  volatile struct npu_wifi_mt7996_fragment_queue_entry *entries;
  volatile uint8_t *packet_cached_memory;
  struct npu_wifi_mt7996_packet_queue_consumer_operations operations;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *diagnostic_counters;
  const volatile uint16_t *retry_limit;
  npu_wifi_mt7996_fragment_queue_delay delay;
  void *operation_context;
  size_t entry_memory_size;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t packet_id_limit;
  uint16_t consumer;
};

struct npu_wifi_mt7996_fragment_queue_consumer {
  volatile struct npu_wifi_mt7996_fragment_queue_entry *entries;
  volatile uint8_t *packet_cached_memory;
  struct npu_wifi_mt7996_packet_queue_consumer_operations operations;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *diagnostic_counters;
  const volatile uint16_t *retry_limit;
  npu_wifi_mt7996_fragment_queue_delay delay;
  void *operation_context;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t packet_id_limit;
  uint32_t valid_sequence_count;
  uint32_t invalid_sequence_count;
  uint32_t forwarded_fragment_count;
  uint32_t forward_failure_count;
  uint32_t packet_release_failure_count;
  uint16_t consumer;
  bool initialized;
};

struct npu_wifi_mt7996_fragment_queue_consumer_result {
  enum npu_runtime_result first_failure;
  uint32_t collected;
  uint32_t forwarded;
  uint32_t forward_failures;
  uint32_t packet_release_failures;
  uint32_t missing_entry_retries;
  uint16_t next_consumer;
  bool stopped_on_empty;
  bool collection_timed_out;
  bool sequence_valid;
  bool pending_work;
};

enum npu_runtime_result npu_wifi_mt7996_fragment_queue_consumer_initialize(
    struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
    const struct npu_wifi_mt7996_fragment_queue_consumer_config *config);
enum npu_runtime_result npu_wifi_mt7996_fragment_queue_consume(
    struct npu_wifi_mt7996_fragment_queue_consumer *consumer,
    struct npu_wifi_mt7996_fragment_queue_consumer_result *result);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_PACKET_QUEUE_CONSUMER_H
#define NPU_WIFI_MT7996_PACKET_QUEUE_CONSUMER_H

#include "an7581/services/wifi/mt7996_packet_queue.h"

#define NPU_WIFI_MT7996_PACKET_QUEUE_PROCESS_LIMIT UINT32_C(0x100)
#define NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE UINT32_C(0x800)
#define NPU_WIFI_MT7996_PACKET_QUEUE_PAYLOAD_OFFSET UINT32_C(0x80)
#define NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_ID_LIMIT UINT32_C(0x4000)

struct npu_wifi_mt7996_packet_delivery {
  uint32_t source_device_address;
  uint32_t packet_word;
  uint16_t packet_id;
  uint16_t total_length;
  uint16_t flow_value;
  uint16_t fragment_length;
  uint8_t delivery_flags;
  uint8_t route;
  bool flag_bit_1;
};

struct npu_wifi_mt7996_packet_queue_consumer_operations {
  enum npu_runtime_result (*forward_packet)(
      void *context, const struct npu_wifi_mt7996_packet_delivery *delivery);
  enum npu_runtime_result (*release_packet)(void *context, uint16_t packet_id);
};

struct npu_wifi_mt7996_packet_queue_consumer_diagnostic_counters {
  volatile uint32_t *entries_retired;
  volatile uint32_t *consume_attempts;
  volatile uint32_t *invalid_packet_ids;
  volatile uint32_t *zero_lengths;
  volatile uint32_t *packets_forwarded;
  volatile uint32_t *forward_failures;
};

struct npu_wifi_mt7996_packet_queue_consumer_config {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entries;
  volatile uint8_t *packet_cached_memory;
  struct npu_wifi_mt7996_packet_queue_consumer_operations operations;
  struct npu_wifi_mt7996_packet_queue_consumer_diagnostic_counters
      diagnostic_counters;
  void *operation_context;
  size_t entry_memory_size;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t packet_id_limit;
  uint16_t consumer;
};

struct npu_wifi_mt7996_packet_queue_consumer {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entries;
  volatile uint8_t *packet_cached_memory;
  struct npu_wifi_mt7996_packet_queue_consumer_operations operations;
  struct npu_wifi_mt7996_packet_queue_consumer_diagnostic_counters
      diagnostic_counters;
  void *operation_context;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t packet_id_limit;
  uint32_t processed_count;
  uint32_t forwarded_count;
  uint32_t forward_failure_count;
  uint32_t invalid_packet_count;
  uint32_t zero_length_count;
  uint32_t packet_release_failure_count;
  uint16_t consumer;
  bool initialized;
};

struct npu_wifi_mt7996_packet_queue_consumer_result {
  enum npu_runtime_result first_failure;
  uint32_t processed;
  uint32_t forwarded;
  uint32_t forward_failures;
  uint32_t invalid_packets;
  uint32_t zero_lengths;
  uint32_t packet_release_failures;
  uint16_t next_consumer;
  bool stopped_on_empty;
};

enum npu_runtime_result npu_wifi_mt7996_packet_queue_consumer_initialize(
    struct npu_wifi_mt7996_packet_queue_consumer *consumer,
    const struct npu_wifi_mt7996_packet_queue_consumer_config *config);
enum npu_runtime_result npu_wifi_mt7996_packet_queue_consume(
    struct npu_wifi_mt7996_packet_queue_consumer *consumer, uint32_t budget,
    struct npu_wifi_mt7996_packet_queue_consumer_result *result);

#endif

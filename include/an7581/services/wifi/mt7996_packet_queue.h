/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_PACKET_QUEUE_H
#define NPU_WIFI_MT7996_PACKET_QUEUE_H

#include "an7581/services/wifi/mt7996_tx_done.h"

#define NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT UINT32_C(0x200)
#define NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_SIZE UINT32_C(12)
#define NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT UINT32_C(0x80)
#define NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_SIZE UINT32_C(12)
#define NPU_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT UINT32_C(2)
#define NPU_WIFI_MT7996_PACKET_QUEUE_OCCUPIED UINT8_C(1)
#define NPU_WIFI_MT7996_PACKET_QUEUE_TX_DONE_FLAGS UINT8_C(3)
#define NPU_WIFI_MT7996_PACKET_QUEUE_UNUSED_PACKET UINT32_MAX

struct npu_wifi_mt7996_packet_queue_entry {
  uint32_t packet_id;
  uint16_t flow_value;
  uint16_t total_length;
  uint16_t fragment_length;
  uint8_t flags;
  uint8_t route;
};

struct npu_wifi_mt7996_fragment_queue_entry {
  uint32_t packet_id;
  uint16_t total_length;
  uint16_t fragment_length;
  uint8_t flags;
  uint8_t reserved[3];
};

typedef enum npu_runtime_result (*npu_wifi_mt7996_packet_queue_lock)(
    void *context);

struct npu_wifi_mt7996_packet_queue_diagnostic_counters {
  volatile uint32_t *entries_enqueued;
  volatile uint32_t *queue_full;
  volatile uint32_t *fragment_entries_enqueued;
};

struct npu_wifi_mt7996_packet_queue_config {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entries;
  volatile struct npu_wifi_mt7996_fragment_queue_entry *fragment_entries;
  npu_wifi_mt7996_packet_queue_lock acquire;
  npu_wifi_mt7996_packet_queue_lock release;
  void *lock_context;
  struct npu_wifi_mt7996_packet_queue_diagnostic_counters diagnostic_counters;
  size_t entry_memory_size;
  size_t fragment_entry_memory_size;
  uint16_t producer;
  uint16_t fragment_producer;
  uint8_t band;
};

struct npu_wifi_mt7996_packet_queue {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entries;
  volatile struct npu_wifi_mt7996_fragment_queue_entry *fragment_entries;
  npu_wifi_mt7996_packet_queue_lock acquire;
  npu_wifi_mt7996_packet_queue_lock release;
  void *lock_context;
  struct npu_wifi_mt7996_packet_queue_diagnostic_counters diagnostic_counters;
  uint32_t enqueued_count;
  uint32_t full_count;
  uint32_t fragment_enqueued_count;
  uint32_t fragment_full_count;
  uint32_t lock_failure_count;
  uint16_t producer;
  uint16_t fragment_producer;
  uint8_t band;
  bool initialized;
};

enum npu_runtime_result npu_wifi_mt7996_packet_queue_initialize(
    struct npu_wifi_mt7996_packet_queue *queue,
    const struct npu_wifi_mt7996_packet_queue_config *config);
enum npu_runtime_result
npu_wifi_mt7996_packet_queue_enqueue(void *context, int16_t packet_id,
                                     uint16_t total_length, uint16_t flow_value,
                                     uint16_t route, uint8_t band,
                                     uint8_t flags, uint16_t fragment_length);
enum npu_runtime_result npu_wifi_mt7996_packet_queue_enqueue_completion(
    void *context, const struct npu_wifi_mt7996_tx_done_completion *completion);

#endif

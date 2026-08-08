/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_TX_DONE_H
#define NPU_WIFI_MT7996_TX_DONE_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"
#include "an7581/services/wifi/tx_ring.h"

#define NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_SIZE UINT32_C(0x10)
#define NPU_WIFI_MT7996_TX_DONE_RING_LIMIT UINT32_C(0x200)
#define NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT UINT32_C(127)
#define NPU_WIFI_MT7996_TX_DONE_PUBLISH_GRANULARITY UINT32_C(16)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE UINT32_C(0x800)
#define NPU_WIFI_MT7996_TX_DONE_RECORD_OFFSET UINT32_C(0x80)
#define NPU_WIFI_MT7996_TX_DONE_RECORD_HEADER_SIZE UINT32_C(12)
#define NPU_WIFI_MT7996_TX_DONE_CACHE_LINE_SIZE UINT32_C(64)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_ID_LIMIT UINT32_C(0x4000)
#define NPU_WIFI_MT7996_TX_DONE_TOKEN_ID_LIMIT UINT32_C(0x8000)
#define NPU_WIFI_MT7996_TX_DONE_PACKET_ID_UNUSED UINT16_C(0xffff)
#define NPU_WIFI_MT7996_TX_DONE_TOKEN_SENTINEL UINT16_C(0x7fff)

struct npu_wifi_mt7996_tx_done_descriptor {
  uint32_t buffer_address;
  uint32_t control;
  uint32_t hardware_status[2];
};

struct npu_wifi_mt7996_tx_done_completion {
  uint16_t packet_id;
  uint16_t packet_length;
};

struct npu_wifi_mt7996_tx_done_operations {
  enum npu_runtime_result (*allocate_packet)(void *context,
                                             uint16_t *packet_id);
  enum npu_runtime_result (*release_packet)(void *context, uint16_t packet_id);
  enum npu_runtime_result (*release_token)(void *context, uint16_t token_id);
  enum npu_runtime_result (*enqueue_packet)(
      void *context,
      const struct npu_wifi_mt7996_tx_done_completion *completion);
  void (*discard_cache_line)(void *context, uint32_t device_address);
};

struct npu_wifi_mt7996_tx_done_config {
  volatile struct npu_wifi_mt7996_tx_done_descriptor *descriptors;
  volatile uint16_t *packet_ids;
  volatile uint8_t *packet_device_memory;
  volatile uint8_t *packet_cached_memory;
  volatile struct npu_wifi_tx_ring_registers *registers;
  struct npu_wifi_mt7996_tx_done_operations operations;
  void *operation_context;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  volatile uint32_t *records_processed_counter;
  volatile uint32_t *invalid_record_type_counter;
  size_t descriptor_memory_size;
  size_t packet_id_memory_size;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t ring_count;
  uint32_t packet_id_limit;
  uint32_t active_token_count;
};

struct npu_wifi_mt7996_tx_done {
  volatile struct npu_wifi_mt7996_tx_done_descriptor *descriptors;
  volatile uint16_t *packet_ids;
  volatile uint8_t *packet_device_memory;
  volatile uint8_t *packet_cached_memory;
  volatile struct npu_wifi_tx_ring_registers *registers;
  struct npu_wifi_mt7996_tx_done_operations operations;
  void *operation_context;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  volatile uint32_t *records_processed_counter;
  volatile uint32_t *invalid_record_type_counter;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t ring_count;
  uint32_t packet_id_limit;
  uint32_t active_token_count;
  uint32_t consumer;
  uint32_t processed_count;
  uint32_t token_release_count;
  uint32_t invalid_token_count;
  uint32_t missing_token_count;
  uint32_t invalid_record_count;
  uint32_t allocation_failure_count;
  uint32_t enqueue_failure_count;
  uint32_t packet_release_failure_count;
  uint32_t token_release_failure_count;
  uint8_t publish_batch;
  bool initialized;
};

struct npu_wifi_mt7996_tx_done_result {
  enum npu_runtime_result stop_status;
  uint32_t processed;
  uint32_t tokens_seen;
  uint32_t tokens_released;
  uint32_t invalid_tokens;
  uint32_t missing_tokens;
  uint32_t invalid_records;
  uint32_t cache_lines_discarded;
  uint32_t consumer_publishes;
  uint32_t packet_release_failures;
  uint32_t token_release_failures;
  uint32_t last_consumed;
  uint32_t next_consumer;
  uint8_t next_publish_batch;
  bool stopped_on_hardware_owned;
  bool stopped_on_allocation_failure;
  bool enqueue_failed;
};

enum npu_runtime_result npu_wifi_mt7996_tx_done_initialize(
    struct npu_wifi_mt7996_tx_done *tx_done,
    const struct npu_wifi_mt7996_tx_done_config *config);
enum npu_runtime_result
npu_wifi_mt7996_tx_done_process(struct npu_wifi_mt7996_tx_done *tx_done,
                                uint32_t budget,
                                struct npu_wifi_mt7996_tx_done_result *result);

#endif

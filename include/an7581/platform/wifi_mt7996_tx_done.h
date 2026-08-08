/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_TX_DONE_H
#define AN7581_WIFI_MT7996_TX_DONE_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/mt7996_tx_done.h"
#include "an7581/services/wifi/packet_id_pool.h"

#define AN7581_WIFI_MT7996_TX_DONE_PACKET_ID_REGION_SIZE UINT32_C(0x800)

typedef enum npu_runtime_result (*an7581_wifi_mt7996_tx_done_enqueue)(
    void *context, const struct npu_wifi_mt7996_tx_done_completion *completion);

struct an7581_wifi_mt7996_tx_done_memory {
  volatile struct npu_wifi_mt7996_tx_done_descriptor *descriptors;
  volatile uint16_t *packet_ids;
  volatile uint8_t *packet_device_memory;
  volatile uint8_t *packet_cached_memory;
  volatile struct npu_wifi_tx_ring_registers *registers;
  size_t descriptor_memory_size;
  size_t packet_id_memory_size;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t ring_count;
  uint32_t packet_id_limit;
  uint32_t active_token_count;
};

struct an7581_wifi_mt7996_tx_done_config {
  struct an7581_wifi_mt7996_tx_done_memory memory;
  struct npu_wifi_packet_id_pool *packet_pool;
  an7581_wifi_mt7996_tx_done_enqueue enqueue;
  void *enqueue_context;
  volatile uint32_t *records_processed_counter;
  volatile uint32_t *invalid_record_type_counter;
};

struct an7581_wifi_mt7996_tx_done_platform {
  struct npu_wifi_mt7996_tx_done service;
  struct npu_wifi_packet_id_pool *packet_pool;
  an7581_wifi_mt7996_tx_done_enqueue enqueue;
  void *enqueue_context;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_tx_done_memory_resolve(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_tx_done_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_tx_done_platform_initialize(
    struct an7581_wifi_mt7996_tx_done_platform *platform,
    const struct an7581_wifi_mt7996_tx_done_config *config);

#endif

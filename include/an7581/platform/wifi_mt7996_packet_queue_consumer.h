/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_PACKET_QUEUE_CONSUMER_H
#define AN7581_WIFI_MT7996_PACKET_QUEUE_CONSUMER_H

#include "an7581/platform/wifi_mt7996_packet_queue.h"
#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/mt7996_packet_queue_consumer.h"
#include "an7581/services/wifi/packet_id_pool.h"

typedef enum npu_runtime_result (*an7581_wifi_mt7996_packet_queue_forward)(
    void *context, const struct npu_wifi_mt7996_packet_delivery *delivery);

struct an7581_wifi_mt7996_packet_queue_consumer_memory {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entries;
  volatile uint8_t *packet_cached_memory;
  size_t entry_memory_size;
  size_t packet_memory_size;
  uint32_t packet_physical_base;
  uint32_t packet_id_limit;
};

struct an7581_wifi_mt7996_packet_queue_consumer_config {
  struct an7581_wifi_mt7996_packet_queue_consumer_memory memory;
  struct npu_wifi_packet_id_pool *packet_pool;
  an7581_wifi_mt7996_packet_queue_forward forward;
  void *forward_context;
  struct npu_wifi_mt7996_packet_queue_consumer_diagnostic_counters
      diagnostic_counters;
  uint16_t consumer;
};

struct an7581_wifi_mt7996_packet_queue_consumer_platform {
  struct npu_wifi_mt7996_packet_queue_consumer service;
  struct npu_wifi_packet_id_pool *packet_pool;
  an7581_wifi_mt7996_packet_queue_forward forward;
  void *forward_context;
  bool initialized;
};

enum npu_runtime_result
an7581_wifi_mt7996_packet_queue_consumer_memory_resolve_band(
    const struct npu_wifi_configuration *configuration, uint32_t band,
    struct an7581_wifi_mt7996_packet_queue_consumer_memory *memory);
enum npu_runtime_result
an7581_wifi_mt7996_packet_queue_consumer_platform_initialize(
    struct an7581_wifi_mt7996_packet_queue_consumer_platform *platform,
    const struct an7581_wifi_mt7996_packet_queue_consumer_config *config);

#endif

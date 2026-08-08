/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_PACKET_QUEUE_H
#define AN7581_WIFI_MT7996_PACKET_QUEUE_H

#include "an7581/platform/hardware_mutex.h"
#include "an7581/services/wifi/mt7996_packet_queue.h"
#include "an7581/services/wifi/region.h"

#define AN7581_WIFI_MT7996_PACKET_QUEUE_REGION_ADDRESS UINT32_C(0x3e8a6040)
#define AN7581_WIFI_MT7996_PACKET_QUEUE_SECONDARY_REGION_ADDRESS               \
  UINT32_C(0x3e8a7858)
#define AN7581_WIFI_MT7996_FRAGMENT_QUEUE_REGION_ADDRESS UINT32_C(0x3e8a9070)
#define AN7581_WIFI_MT7996_FRAGMENT_QUEUE_SECONDARY_REGION_ADDRESS             \
  UINT32_C(0x3e8a9670)
#define AN7581_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT UINT32_C(2)
#define AN7581_WIFI_MT7996_PACKET_QUEUE_MEMORY_SIZE UINT32_C(0x1800)
#define AN7581_WIFI_MT7996_FRAGMENT_QUEUE_MEMORY_SIZE UINT32_C(0x600)
#define AN7581_WIFI_MT7996_PACKET_QUEUE_MUTEX_HANDLE UINT32_C(0x1a)

struct an7581_wifi_mt7996_packet_queue_memory {
  volatile struct npu_wifi_mt7996_packet_queue_entry *entries;
  volatile struct npu_wifi_mt7996_fragment_queue_entry *fragment_entries;
  size_t entry_memory_size;
  size_t fragment_entry_memory_size;
};

struct an7581_wifi_mt7996_packet_queue_config {
  struct an7581_wifi_mt7996_packet_queue_memory memory;
  struct npu_wifi_mt7996_packet_queue_diagnostic_counters diagnostic_counters;
  uint32_t hart_id;
  uint16_t producer;
  uint16_t fragment_producer;
  uint8_t band;
};

struct an7581_wifi_mt7996_packet_queue_platform {
  struct an7581_hardware_mutex_bank mutexes;
  struct npu_wifi_mt7996_packet_queue service;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_packet_queue_memory_resolve(
    struct an7581_wifi_mt7996_packet_queue_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_packet_queue_memory_resolve_band(
    uint32_t band, struct an7581_wifi_mt7996_packet_queue_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_packet_queue_platform_initialize(
    struct an7581_wifi_mt7996_packet_queue_platform *platform,
    const struct an7581_wifi_mt7996_packet_queue_config *config);

#endif

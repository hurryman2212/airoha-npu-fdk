/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_TDMA_DELIVERY_H
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_H

#include "an7581/services/wifi/mt7996_tdma_delivery.h"

#define AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_BASE UINT32_C(0x3e880000)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_MEMORY_SIZE                \
  (NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT * NPU_WIFI_MT7996_TDMA_DESCRIPTOR_SIZE)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_REGISTERS_ADDRESS UINT32_C(0x1fb50800)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_GROUP_ADDRESS UINT32_C(0x1fb50a2c)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_ENABLE_ADDRESS                   \
  UINT32_C(0x1fb50a28)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_GLOBAL_ENABLE_ADDRESS                 \
  UINT32_C(0x1fb50a04)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_BASE_MASK                  \
  UINT32_C(0x1fffffff)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_TEMPLATE                   \
  UINT32_C(0x7f4087ff)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_GROUP_VALUE UINT32_C(0x01010101)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_ENABLE_VALUE UINT32_C(1)
#define AN7581_WIFI_MT7996_TDMA_DELIVERY_GLOBAL_ENABLE_MASK UINT32_C(0x71)

struct an7581_wifi_mt7996_tdma_delivery_memory {
  volatile struct an7581_qdma_descriptor *descriptors;
  volatile struct npu_wifi_tx_ring_registers *registers;
  volatile uint32_t *band_group;
  volatile uint32_t *band_enable;
  volatile uint32_t *global_enable;
  size_t descriptor_memory_size;
  uint32_t descriptor_physical_base;
};

struct an7581_wifi_mt7996_tdma_delivery_config {
  struct an7581_wifi_mt7996_tdma_delivery_memory memory;
  volatile uint8_t *packet_mapping;
  npu_wifi_mt7996_packet_queue_enqueue_callback enqueue;
  npu_wifi_rro_cpu_queue_release release;
  void *packet_context;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *diagnostic_counters;
  size_t packet_mapping_size;
  uint32_t packet_dma_base;
  uint32_t packet_count;
};

struct an7581_wifi_mt7996_tdma_delivery_platform {
  struct npu_wifi_mt7996_tdma_delivery delivery;
  struct an7581_wifi_mt7996_tdma_delivery_memory memory;
  bool hardware_published;
  bool initialized;
};

bool an7581_wifi_mt7996_tdma_delivery_memory_is_valid(
    const struct an7581_wifi_mt7996_tdma_delivery_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_tdma_delivery_memory_resolve(
    struct an7581_wifi_mt7996_tdma_delivery_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_tdma_delivery_platform_initialize(
    struct an7581_wifi_mt7996_tdma_delivery_platform *platform,
    const struct an7581_wifi_mt7996_tdma_delivery_config *config);

#endif

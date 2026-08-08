/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_REGION_H
#define NPU_WIFI_REGION_H

#include "an7581/platform/memory_map.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"
#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_SRAM_BASE AN7581_NPU_SHARED_SRAM_BASE
#define NPU_WIFI_SRAM_SIZE AN7581_NPU_SHARED_SRAM_SIZE
#define NPU_WIFI_SRAM_ALLOCATION_LIMIT 100U
#define NPU_WIFI_MT7996_DYNAMIC_ARENA_SIZE UINT32_C(0x0004a040)

enum npu_wifi_mt7996_sram_region_type {
  NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA = 0x01,
  NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND1 = 0x09,
  NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND0 = 0x0a,
  NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND2 = 0x0b,
  NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING = 0x12,
  NPU_WIFI_MT7996_SRAM_RRO_CPU_QUEUE = 0x19,
  NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH = 0x1d,
  NPU_WIFI_MT7996_SRAM_TUNNEL_PACKETS = 0x81,
  NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE = 0x8a,
};

enum npu_wifi_mt7996_fixed_region_type {
  NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_BAND0 = 0x100,
  NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_SECONDARY = 0x101,
  NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_BAND0 = 0x102,
  NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_SECONDARY = 0x103,
  NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_BAND0 = 0x104,
  NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_SECONDARY = 0x105,
  NPU_WIFI_MT7996_FIXED_MSDU_PAGE_ID_MAP = 0x106,
  NPU_WIFI_MT7996_FIXED_TX_DONE_PACKET_ID_MAP = 0x107,
  NPU_WIFI_MT7996_FIXED_ICV_ERROR_TABLE = 0x109,
  NPU_WIFI_MT7996_FIXED_TDMA_DELIVERY_DESCRIPTORS = 0x181,
  NPU_WIFI_MT7996_FIXED_TDM_RX_DESCRIPTORS = 0x182,
};

enum npu_wifi_mt7996_dynamic_region_type {
  NPU_WIFI_MT7996_DYNAMIC_PRIMARY_EAGLE_RX = 1,
  NPU_WIFI_MT7996_DYNAMIC_SECONDARY_EAGLE_RX = 2,
  NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_BAND0 = 3,
  NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_SECONDARY = 4,
  NPU_WIFI_MT7996_DYNAMIC_RRO_INDICATIONS = 6,
  NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND0 = 7,
  NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND1 = 8,
  NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND2 = 9,
  NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_BAND0 = 10,
  NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_SECONDARY = 11,
};

struct npu_wifi_region {
  uint32_t type;
  uint32_t address;
  uint32_t usable_size;
  uint32_t reserved_size;
};

struct npu_wifi_sram_allocation {
  uint32_t type;
  uint32_t address;
};

typedef enum npu_runtime_result (*npu_wifi_sram_allocator_lock_operation)(
    void *context, uint32_t lock_index);

struct npu_wifi_sram_allocator {
  struct npu_wifi_sram_allocation allocations[NPU_WIFI_SRAM_ALLOCATION_LIMIT];
  npu_wifi_sram_allocator_lock_operation acquire;
  npu_wifi_sram_allocator_lock_operation release;
  void *lock_context;
  uint32_t current_offset;
  uint32_t allocation_count;
  uint32_t lock_index;
};

void npu_wifi_sram_allocator_reset(struct npu_wifi_sram_allocator *allocator);
enum npu_runtime_result npu_wifi_sram_allocator_configure_lock(
    struct npu_wifi_sram_allocator *allocator,
    npu_wifi_sram_allocator_lock_operation acquire,
    npu_wifi_sram_allocator_lock_operation release, void *lock_context,
    uint32_t lock_index);
bool npu_wifi_sram_allocate(struct npu_wifi_sram_allocator *allocator,
                            uint32_t type, struct npu_wifi_region *region);
bool npu_wifi_mt7996_fixed_region_lookup(uint32_t type,
                                         struct npu_wifi_region *region);
bool npu_wifi_mt7996_region_lookup(struct npu_wifi_sram_allocator *allocator,
                                   uint32_t type,
                                   struct npu_wifi_region *region);
bool npu_wifi_mt7996_dynamic_region_lookup(uint32_t dynamic_base, uint32_t type,
                                           struct npu_wifi_region *region);
bool npu_wifi_rx_ring_region_lookup(uint32_t dynamic_base,
                                    uint32_t set_interface,
                                    struct npu_wifi_region *region);

#endif

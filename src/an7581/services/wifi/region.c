/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/region.h"

#include "an7581/runtime/memory.h"

#define NPU_WIFI_REGION_ALIGNMENT UINT32_C(0x20)

struct region_size {
  uint32_t type;
  uint32_t size;
};

struct fixed_region {
  uint32_t type;
  uint32_t address;
};

struct relative_region {
  uint32_t type;
  uint32_t offset;
  uint32_t usable_size;
  uint32_t reserved_size;
};

static const struct region_size mt7996_low_sram_regions[] = {
    {NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA, UINT32_C(0x0004a040)},
    {NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND1, UINT32_C(0x000003e8)},
    {NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND0, UINT32_C(0x000003e8)},
    {NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND2, UINT32_C(0x00000078)},
    {NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING, UINT32_C(0x0000e000)},
    {NPU_WIFI_MT7996_SRAM_RRO_CPU_QUEUE, UINT32_C(0x00008010)},
    {NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH, UINT32_C(0x00001000)},
};

static const struct region_size mt7996_high_sram_regions[] = {
    {NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE, UINT32_C(0x00008000)},
    {NPU_WIFI_MT7996_SRAM_TUNNEL_PACKETS, UINT32_C(0x0000e5ff)},
};

static const struct fixed_region mt7996_fixed_regions[] = {
    {NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_BAND0, UINT32_C(0x3e8a2000)},
    {NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_SECONDARY,
     UINT32_C(0x3e8a4020)},
    {NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_BAND0, UINT32_C(0x3e8a6040)},
    {NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_SECONDARY, UINT32_C(0x3e8a7858)},
    {NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_BAND0, UINT32_C(0x3e8a9070)},
    {NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_SECONDARY, UINT32_C(0x3e8a9670)},
    {NPU_WIFI_MT7996_FIXED_MSDU_PAGE_ID_MAP, UINT32_C(0x3e8a9c70)},
    {NPU_WIFI_MT7996_FIXED_TX_DONE_PACKET_ID_MAP, UINT32_C(0x3e8adc70)},
    {NPU_WIFI_MT7996_FIXED_ICV_ERROR_TABLE, UINT32_C(0x3e8bc470)},
    {NPU_WIFI_MT7996_FIXED_TDMA_DELIVERY_DESCRIPTORS, UINT32_C(0x3e880000)},
    {NPU_WIFI_MT7996_FIXED_TDM_RX_DESCRIPTORS, UINT32_C(0x3e891000)},
};

static const struct relative_region mt7996_dynamic_regions[] = {
    {NPU_WIFI_MT7996_DYNAMIC_PRIMARY_EAGLE_RX, UINT32_C(0x00000000),
     UINT32_C(0x00006000), UINT32_C(0x00006020)},
    {NPU_WIFI_MT7996_DYNAMIC_SECONDARY_EAGLE_RX, UINT32_C(0x00022020),
     UINT32_C(0x00004000), UINT32_C(0x00004020)},
    {NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_BAND0, UINT32_C(0x00006020),
     UINT32_C(0x00002000), UINT32_C(0x00002000)},
    {NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_SECONDARY, UINT32_C(0x00026040),
     UINT32_C(0x00004000), UINT32_C(0x00004000)},
    {NPU_WIFI_MT7996_DYNAMIC_RRO_INDICATIONS, UINT32_C(0x0001f020),
     UINT32_C(0x00003000), UINT32_C(0x00003000)},
    {NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND0, UINT32_C(0x00018020),
     UINT32_C(0x00001000), UINT32_C(0x00001000)},
    {NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND1, UINT32_C(0x00019020),
     UINT32_C(0x00002000), UINT32_C(0x00002000)},
    {NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND2, UINT32_C(0x0001b020),
     UINT32_C(0x00004000), UINT32_C(0x00004000)},
    {NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_BAND0, UINT32_C(0x00008020),
     UINT32_C(0x00010000), UINT32_C(0x00010000)},
    {NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_SECONDARY, UINT32_C(0x0002a040),
     UINT32_C(0x00020000), UINT32_C(0x00020000)},
};

static const struct region_size *
find_region_size(const struct region_size *regions, size_t region_count,
                 uint32_t type) {
  size_t index;

  for (index = 0U; index < region_count; ++index) {
    if (regions[index].type == type)
      return &regions[index];
  }
  return NULL;
}

static const struct fixed_region *
find_fixed_region(const struct fixed_region *regions, size_t region_count,
                  uint32_t type) {
  size_t index;

  for (index = 0U; index < region_count; ++index) {
    if (regions[index].type == type)
      return &regions[index];
  }
  return NULL;
}

static const struct relative_region *
find_relative_region(const struct relative_region *regions, size_t region_count,
                     uint32_t type) {
  size_t index;

  for (index = 0U; index < region_count; ++index) {
    if (regions[index].type == type)
      return &regions[index];
  }
  return NULL;
}

static bool fixed_region_lookup(const struct fixed_region *regions,
                                size_t region_count, uint32_t type,
                                struct npu_wifi_region *region) {
  const struct fixed_region *fixed;

  if (region == NULL)
    return false;

  fixed = find_fixed_region(regions, region_count, type);
  if (fixed == NULL)
    return false;

  region->type = type;
  region->address = fixed->address;
  region->usable_size = 0U;
  region->reserved_size = 0U;
  return true;
}

void npu_wifi_sram_allocator_reset(struct npu_wifi_sram_allocator *allocator) {
  if (allocator == NULL)
    return;

  (void)npu_memset(allocator, 0U, sizeof(*allocator));
}

enum npu_runtime_result npu_wifi_sram_allocator_configure_lock(
    struct npu_wifi_sram_allocator *allocator,
    npu_wifi_sram_allocator_lock_operation acquire,
    npu_wifi_sram_allocator_lock_operation release, void *lock_context,
    uint32_t lock_index) {
  if (allocator == NULL || acquire == NULL || release == NULL ||
      lock_context == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (allocator->allocation_count != 0U || allocator->current_offset != 0U ||
      allocator->acquire != NULL || allocator->release != NULL)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  allocator->acquire = acquire;
  allocator->release = release;
  allocator->lock_context = lock_context;
  allocator->lock_index = lock_index;
  return NPU_RUNTIME_SUCCESS;
}

bool npu_wifi_sram_allocate(struct npu_wifi_sram_allocator *allocator,
                            uint32_t type, struct npu_wifi_region *region) {
  const struct region_size *size;
  bool allocated = false;
  uint32_t aligned_offset;
  uint32_t index;

  if (allocator == NULL || region == NULL ||
      allocator->allocation_count > NPU_WIFI_SRAM_ALLOCATION_LIMIT ||
      (allocator->acquire == NULL) != (allocator->release == NULL))
    return false;
  if (allocator->acquire != NULL &&
      allocator->acquire(allocator->lock_context, allocator->lock_index) !=
          NPU_RUNTIME_SUCCESS)
    return false;

  for (index = 0U; index < allocator->allocation_count; ++index) {
    if (allocator->allocations[index].type == type) {
      region->type = type;
      region->address = allocator->allocations[index].address;
      size = type < UINT32_C(0x81)
                 ? find_region_size(mt7996_low_sram_regions,
                                    sizeof(mt7996_low_sram_regions) /
                                        sizeof(mt7996_low_sram_regions[0]),
                                    type)
                 : find_region_size(mt7996_high_sram_regions,
                                    sizeof(mt7996_high_sram_regions) /
                                        sizeof(mt7996_high_sram_regions[0]),
                                    type);
      if (size == NULL)
        goto release;
      region->usable_size = size->size;
      region->reserved_size = size->size;
      allocated = true;
      goto release;
    }
  }

  size = type < UINT32_C(0x81)
             ? find_region_size(mt7996_low_sram_regions,
                                sizeof(mt7996_low_sram_regions) /
                                    sizeof(mt7996_low_sram_regions[0]),
                                type)
             : find_region_size(mt7996_high_sram_regions,
                                sizeof(mt7996_high_sram_regions) /
                                    sizeof(mt7996_high_sram_regions[0]),
                                type);
  if (size == NULL ||
      allocator->allocation_count >= NPU_WIFI_SRAM_ALLOCATION_LIMIT ||
      allocator->current_offset > NPU_WIFI_SRAM_SIZE)
    goto release;

  aligned_offset =
      (allocator->current_offset + NPU_WIFI_REGION_ALIGNMENT - 1U) &
      ~(NPU_WIFI_REGION_ALIGNMENT - 1U);
  if (aligned_offset > NPU_WIFI_SRAM_SIZE ||
      size->size > NPU_WIFI_SRAM_SIZE - aligned_offset)
    goto release;

  region->type = type;
  region->address = NPU_WIFI_SRAM_BASE + aligned_offset;
  region->usable_size = size->size;
  region->reserved_size = size->size;
  allocator->allocations[allocator->allocation_count].type = type;
  allocator->allocations[allocator->allocation_count].address = region->address;
  ++allocator->allocation_count;
  allocator->current_offset = aligned_offset + size->size;
  allocated = true;

release:
  if (allocator->release != NULL &&
      allocator->release(allocator->lock_context, allocator->lock_index) !=
          NPU_RUNTIME_SUCCESS)
    return false;
  return allocated;
}

bool npu_wifi_mt7996_fixed_region_lookup(uint32_t type,
                                         struct npu_wifi_region *region) {
  return fixed_region_lookup(mt7996_fixed_regions,
                             sizeof(mt7996_fixed_regions) /
                                 sizeof(mt7996_fixed_regions[0]),
                             type, region);
}

bool npu_wifi_mt7996_region_lookup(struct npu_wifi_sram_allocator *allocator,
                                   uint32_t type,
                                   struct npu_wifi_region *region) {
  if (type < UINT32_C(0x100))
    return npu_wifi_sram_allocate(allocator, type, region);
  return npu_wifi_mt7996_fixed_region_lookup(type, region);
}

bool npu_wifi_mt7996_dynamic_region_lookup(uint32_t dynamic_base, uint32_t type,
                                           struct npu_wifi_region *region) {
  const struct relative_region *relative;

  if (dynamic_base == 0U ||
      (dynamic_base & (NPU_WIFI_REGION_ALIGNMENT - 1U)) != 0U || region == NULL)
    return false;

  relative = find_relative_region(
      mt7996_dynamic_regions,
      sizeof(mt7996_dynamic_regions) / sizeof(mt7996_dynamic_regions[0]), type);
  if (relative == NULL || dynamic_base > UINT32_MAX - relative->offset ||
      dynamic_base + relative->offset > UINT32_MAX - relative->reserved_size)
    return false;

  region->type = type;
  region->address = dynamic_base + relative->offset;
  region->usable_size = relative->usable_size;
  region->reserved_size = relative->reserved_size;
  return true;
}

static bool mt7996_rx_ring_region_lookup(uint32_t dynamic_base,
                                         uint32_t set_interface,
                                         struct npu_wifi_region *region) {
  uint32_t type;

  switch (set_interface) {
  case NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE:
    type = NPU_WIFI_MT7996_DYNAMIC_PRIMARY_EAGLE_RX;
    break;
  case NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE:
    type = NPU_WIFI_MT7996_DYNAMIC_SECONDARY_EAGLE_RX;
    break;
  case NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_INTERFACE:
    type = NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND0;
    break;
  case NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_INTERFACE:
    type = NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND1;
    break;
  case NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_INTERFACE:
    type = NPU_WIFI_MT7996_DYNAMIC_MSDU_PAGE_DESCRIPTORS_BAND2;
    break;
  case NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE:
    type = NPU_WIFI_MT7996_DYNAMIC_RRO_INDICATIONS;
    break;
  default:
    return false;
  }

  return npu_wifi_mt7996_dynamic_region_lookup(dynamic_base, type, region);
}

bool npu_wifi_rx_ring_region_lookup(uint32_t dynamic_base,
                                    uint32_t set_interface,
                                    struct npu_wifi_region *region) {
  if (region == NULL)
    return false;

  return mt7996_rx_ring_region_lookup(dynamic_base, set_interface, region);
}

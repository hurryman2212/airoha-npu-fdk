/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_packet_space.h"

#include "an7581/platform/dma.h"

_Static_assert(sizeof(struct npu_wifi_tx_packet_descriptor) ==
                   NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE,
               "Wi-Fi TX packet descriptor layout changed");

#define PACKET_PROFILE(type_value, index_value)                                \
  {                                                                            \
      .region_type = (type_value),                                             \
      .descriptor_count = NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT,                 \
      .descriptor_size = NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE,                   \
      .activation_index = (index_value),                                       \
  }

static const struct npu_wifi_tx_packet_space_profile profiles[] = {
    PACKET_PROFILE(NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_BAND0, 0U),
    PACKET_PROFILE(NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_SECONDARY, 1U),
};

static const struct npu_wifi_tx_packet_space_profile *
find_profile(const struct npu_wifi_tx_packet_space_profile *entries,
             size_t profile_count, uint32_t activation_index) {
  size_t index;

  for (index = 0U; index < profile_count; ++index) {
    if ((uint32_t)entries[index].activation_index == activation_index)
      return &entries[index];
  }
  return NULL;
}

const struct npu_wifi_tx_packet_space_profile *
npu_wifi_tx_packet_space_find_profile(uint32_t activation_index) {
  return find_profile(profiles, sizeof(profiles) / sizeof(profiles[0]),
                      activation_index);
}

bool npu_wifi_tx_packet_space_region_lookup(uint32_t activation_index,
                                            struct npu_wifi_region *region) {
  const struct npu_wifi_tx_packet_space_profile *profile =
      npu_wifi_tx_packet_space_find_profile(activation_index);

  if (profile == NULL || region == NULL)
    return false;

  if (!npu_wifi_mt7996_fixed_region_lookup(profile->region_type, region))
    return false;

  region->usable_size = profile->descriptor_count * profile->descriptor_size;
  region->reserved_size = region->usable_size + UINT32_C(0x20);
  return true;
}

enum npu_runtime_result npu_wifi_tx_packet_space_initialize(
    const struct npu_wifi_tx_packet_space_profile *profile,
    uint32_t packet_space_address, void *descriptor_memory,
    size_t descriptor_memory_size) {
  struct npu_wifi_tx_packet_descriptor *descriptors = descriptor_memory;
  uint32_t local_address;
  size_t required_size;
  uint32_t index;

  if (profile == NULL ||
      profile->descriptor_count != NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT ||
      profile->descriptor_size != NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (packet_space_address == 0U ||
      packet_space_address > NPU_WIFI_TX_PACKET_MAX_HOST_ADDRESS ||
      descriptor_memory == NULL ||
      ((uintptr_t)descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      !an7581_dma_buffer_map(packet_space_address,
                             profile->descriptor_count *
                                 NPU_WIFI_TX_PACKET_BUFFER_STRIDE,
                             NPU_WIFI_TX_PACKET_BUFFER_STRIDE, &local_address))
    return NPU_RUNTIME_INVALID_ARGUMENT;

  required_size = (size_t)profile->descriptor_count * profile->descriptor_size;
  if (descriptor_memory_size < required_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < profile->descriptor_count; ++index) {
    descriptors[index].buffer_address =
        ((local_address + index * NPU_WIFI_TX_PACKET_BUFFER_STRIDE) &
         AN7581_DMA_PHYSICAL_MASK) |
        NPU_WIFI_TX_PACKET_BUFFER_OWNED;
    descriptors[index].packet_address = 0U;
    descriptors[index].token_control = 0U;
    descriptors[index].status &= UINT32_C(0xffffff00);
  }
  return NPU_RUNTIME_SUCCESS;
}

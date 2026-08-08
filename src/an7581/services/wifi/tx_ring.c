/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_ring.h"

#include "an7581/platform/dma.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"

_Static_assert(sizeof(struct npu_wifi_tx_descriptor) ==
                   NPU_WIFI_TX_DESCRIPTOR_SIZE,
               "Wi-Fi TX descriptor layout changed");

#define TX_PROFILE(type_value, count, get_id)                                  \
  {                                                                            \
      .region_type = (type_value),                                             \
      .descriptor_count = (count),                                             \
      .descriptor_size = NPU_WIFI_TX_DESCRIPTOR_SIZE,                          \
      .get_interface = (get_id),                                               \
  }

static const struct npu_wifi_tx_ring_profile profiles[] = {
    TX_PROFILE(NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_BAND0, UINT32_C(0x200),
               NPU_WIFI_MT7996_TX_FREE_POINTER_BAND0_INTERFACE),
    TX_PROFILE(NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_SECONDARY,
               UINT32_C(0x400),
               NPU_WIFI_MT7996_TX_FREE_POINTER_BAND2_INTERFACE),
};

static const struct npu_wifi_tx_ring_profile *
find_profile(const struct npu_wifi_tx_ring_profile *entries,
             size_t profile_count, uint32_t get_interface) {
  size_t index;

  for (index = 0U; index < profile_count; ++index) {
    if ((uint32_t)entries[index].get_interface == get_interface)
      return &entries[index];
  }
  return NULL;
}

const struct npu_wifi_tx_ring_profile *
npu_wifi_tx_ring_find_profile(uint32_t get_interface) {
  return find_profile(profiles, sizeof(profiles) / sizeof(profiles[0]),
                      get_interface);
}

bool npu_wifi_tx_ring_region_lookup(uint32_t dynamic_base,
                                    uint32_t get_interface,
                                    struct npu_wifi_region *region) {
  const struct npu_wifi_tx_ring_profile *profile =
      npu_wifi_tx_ring_find_profile(get_interface);

  if (profile == NULL || region == NULL)
    return false;

  return npu_wifi_mt7996_dynamic_region_lookup(dynamic_base,
                                               profile->region_type, region);
}

enum npu_runtime_result
npu_wifi_tx_ring_prime(const struct npu_wifi_tx_ring_profile *profile,
                       void *descriptor_memory, size_t descriptor_memory_size) {
  struct npu_wifi_tx_descriptor *descriptors = descriptor_memory;
  size_t required_size;
  uint32_t index;

  if (profile == NULL || profile->descriptor_count == 0U ||
      profile->descriptor_size != NPU_WIFI_TX_DESCRIPTOR_SIZE)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (descriptor_memory == NULL ||
      ((uintptr_t)descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  required_size = (size_t)profile->descriptor_count * profile->descriptor_size;
  if (descriptor_memory_size < required_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < profile->descriptor_count; ++index)
    descriptors[index].control = NPU_WIFI_TX_DESCRIPTOR_READY;
  return NPU_RUNTIME_SUCCESS;
}

bool npu_wifi_tx_ring_store_descriptor_base(
    struct npu_wifi_tx_descriptor_base_state *state, uint32_t physical_address,
    uint32_t band) {
  size_t slot;

  if (state == NULL)
    return false;

  slot = band == 0U ? 0U : 1U;
  state->local_address[slot] = an7581_dma_local_alias(physical_address);
  return true;
}

bool npu_wifi_tx_ring_set_descriptor_base_sram(
    struct npu_wifi_tx_descriptor_base_state *state, uint32_t physical_address,
    uint32_t band, npu_wifi_tx_ring_sram_warning report_warning,
    void *warning_context) {
  if (!npu_wifi_tx_ring_store_descriptor_base(state, physical_address, band))
    return false;

  if (report_warning != NULL)
    report_warning(warning_context);
  return true;
}

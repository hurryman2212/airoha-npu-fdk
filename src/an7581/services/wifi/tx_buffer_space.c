/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_buffer_space.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"

_Static_assert(sizeof(struct npu_wifi_tx_buffer_space_record) ==
                   NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE,
               "Wi-Fi TX buffer-space record layout changed");

#define ADDRESS_PROFILE(role_value, interface_value, index_value)              \
  {                                                                            \
      .role = (role_value),                                                    \
      .storage = NPU_WIFI_TX_BUFFER_SPACE_EXTERNAL,                            \
      .set_interface = (interface_value),                                      \
      .activation_index = (index_value),                                       \
  }

#define TABLE_PROFILE(storage_value, type_value, address_value, count_value,   \
                      interface_value)                                         \
  {                                                                            \
      .role = NPU_WIFI_TX_BUFFER_SPACE_FREE_POINTER_TABLE,                     \
      .storage = (storage_value),                                              \
      .dynamic_region_type = (type_value),                                     \
      .fixed_address = (address_value),                                        \
      .record_count = (count_value),                                           \
      .record_size = NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE,                     \
      .set_interface = (interface_value),                                      \
  }

static const struct npu_wifi_tx_buffer_space_profile profiles[] = {
    ADDRESS_PROFILE(NPU_WIFI_TX_BUFFER_SPACE_QUEUE_BASE,
                    NPU_WIFI_MT7996_TX_QUEUE_BAND0_INTERFACE, 0U),
    ADDRESS_PROFILE(NPU_WIFI_TX_BUFFER_SPACE_QUEUE_BASE,
                    NPU_WIFI_MT7996_TX_QUEUE_BAND2_INTERFACE, 1U),
    TABLE_PROFILE(NPU_WIFI_TX_BUFFER_SPACE_DYNAMIC_REGION,
                  NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_BAND0, 0U,
                  UINT32_C(0x200),
                  NPU_WIFI_MT7996_TX_FREE_POINTER_BAND0_INTERFACE),
    TABLE_PROFILE(NPU_WIFI_TX_BUFFER_SPACE_DYNAMIC_REGION,
                  NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_SECONDARY, 0U,
                  UINT32_C(0x400),
                  NPU_WIFI_MT7996_TX_FREE_POINTER_BAND2_INTERFACE),
    ADDRESS_PROFILE(NPU_WIFI_TX_BUFFER_SPACE_PACKET_BASE,
                    NPU_WIFI_MT7996_TX_PACKET_BAND0_INTERFACE, 0U),
    ADDRESS_PROFILE(NPU_WIFI_TX_BUFFER_SPACE_PACKET_BASE,
                    NPU_WIFI_MT7996_TX_PACKET_BAND2_INTERFACE, 1U),
};

static const struct npu_wifi_tx_buffer_space_profile *
find_profile(const struct npu_wifi_tx_buffer_space_profile *entries,
             size_t profile_count, uint32_t set_interface) {
  size_t index;

  for (index = 0U; index < profile_count; ++index) {
    if ((uint32_t)entries[index].set_interface == set_interface)
      return &entries[index];
  }
  return NULL;
}

const struct npu_wifi_tx_buffer_space_profile *
npu_wifi_tx_buffer_space_find_profile(uint32_t set_interface) {
  return find_profile(profiles, sizeof(profiles) / sizeof(profiles[0]),
                      set_interface);
}

bool npu_wifi_tx_buffer_space_region_lookup(
    const struct npu_wifi_tx_buffer_space_profile *profile,
    uint32_t dynamic_base, uint32_t host_address,
    struct npu_wifi_region *region) {
  uint32_t local_address;
  uint32_t required_size;

  if (profile == NULL ||
      profile->role != NPU_WIFI_TX_BUFFER_SPACE_FREE_POINTER_TABLE ||
      profile->record_count == 0U ||
      profile->record_size != NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE ||
      host_address == 0U ||
      host_address > NPU_WIFI_TX_BUFFER_SPACE_MAX_HOST_ADDRESS ||
      region == NULL)
    return false;

  required_size = profile->record_count * profile->record_size;
  if (profile->storage == NPU_WIFI_TX_BUFFER_SPACE_DYNAMIC_REGION)
    return npu_wifi_mt7996_dynamic_region_lookup(
        dynamic_base, profile->dynamic_region_type, region);
  if (profile->storage == NPU_WIFI_TX_BUFFER_SPACE_FIXED_ADDRESS) {
    if (profile->fixed_address == 0U)
      return false;

    region->type = 0U;
    region->address = profile->fixed_address;
    region->usable_size = required_size;
    region->reserved_size = required_size;
    return true;
  }
  if (profile->storage != NPU_WIFI_TX_BUFFER_SPACE_EXTERNAL ||
      !an7581_dma_buffer_map(host_address, required_size, sizeof(uint32_t),
                             &local_address))
    return false;

  region->type = 0U;
  region->address = host_address;
  region->usable_size = required_size;
  region->reserved_size = required_size;
  return true;
}

enum npu_runtime_result npu_wifi_tx_buffer_space_initialize(
    const struct npu_wifi_tx_buffer_space_profile *profile, void *record_memory,
    size_t record_memory_size) {
  struct npu_wifi_tx_buffer_space_record *records = record_memory;
  size_t required_size;
  uint32_t index;

  if (profile == NULL ||
      profile->role != NPU_WIFI_TX_BUFFER_SPACE_FREE_POINTER_TABLE ||
      profile->record_count == 0U ||
      profile->record_size != NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (record_memory == NULL ||
      ((uintptr_t)record_memory & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  required_size = (size_t)profile->record_count * profile->record_size;
  if (record_memory_size < required_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < profile->record_count; ++index) {
    (void)npu_memset(&records[index], 0U, sizeof(records[index]));
    records[index].hardware_head_control =
        NPU_WIFI_TX_BUFFER_SPACE_HEAD_CONTROL_INITIAL_VALUE;
    records[index].hardware_tail_control =
        NPU_WIFI_TX_BUFFER_SPACE_TAIL_CONTROL_INITIAL_VALUE;
  }
  return NPU_RUNTIME_SUCCESS;
}

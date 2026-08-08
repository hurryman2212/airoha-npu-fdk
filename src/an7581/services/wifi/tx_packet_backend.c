/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_packet_backend.h"

static struct npu_wifi_tx_packet_arena *
find_arena(struct npu_wifi_tx_packet_static_backend *backend,
           uint32_t activation_index) {
  size_t index;

  if (backend == NULL || backend->arenas == NULL)
    return NULL;

  for (index = 0U; index < backend->arena_count; ++index) {
    if ((uint32_t)backend->arenas[index].activation_index == activation_index)
      return &backend->arenas[index];
  }
  return NULL;
}

bool npu_wifi_tx_packet_static_backend_activate(void *context,
                                                uint32_t activation_index,
                                                uint32_t local_address) {
  struct npu_wifi_tx_packet_static_backend *backend = context;
  const struct npu_wifi_tx_packet_space_profile *profile;
  struct npu_wifi_tx_packet_arena *arena;
  struct npu_wifi_region region;
  size_t required_size;

  if (backend == NULL)
    return false;

  profile = npu_wifi_tx_packet_space_find_profile(activation_index);
  arena = find_arena(backend, activation_index);
  if (profile == NULL || arena == NULL || arena->initialized ||
      !npu_wifi_tx_packet_space_region_lookup(activation_index, &region) ||
      arena->physical_base != region.address)
    return false;

  required_size = (size_t)profile->descriptor_count * profile->descriptor_size;
  if (required_size != region.usable_size ||
      arena->descriptor_memory_size < required_size ||
      arena->descriptor_memory_size > region.reserved_size)
    return false;
  if (npu_wifi_tx_packet_space_initialize(
          profile, local_address, arena->descriptor_memory,
          arena->descriptor_memory_size) != NPU_RUNTIME_SUCCESS)
    return false;

  arena->initialized = true;
  return true;
}

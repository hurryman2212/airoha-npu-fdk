/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_buffer_backend.h"

#include "an7581/platform/dma.h"

static struct npu_wifi_tx_buffer_space_arena *
find_arena(struct npu_wifi_tx_buffer_space_backend *backend,
           uint32_t set_interface) {
  size_t index;

  if (backend == NULL || backend->arenas == NULL)
    return NULL;

  for (index = 0U; index < backend->arena_count; ++index) {
    if ((uint32_t)backend->arenas[index].set_interface == set_interface)
      return &backend->arenas[index];
  }
  return NULL;
}

static bool
set_address_profile(struct npu_wifi_tx_buffer_space_backend *backend,
                    const struct npu_wifi_tx_buffer_space_profile *profile,
                    uint32_t host_address) {
  uint32_t local_address;

  if (host_address == 0U ||
      host_address > NPU_WIFI_TX_BUFFER_SPACE_MAX_HOST_ADDRESS ||
      !an7581_dma_buffer_map(host_address, sizeof(uint32_t), sizeof(uint32_t),
                             &local_address))
    return false;
  if (profile->role == NPU_WIFI_TX_BUFFER_SPACE_PACKET_BASE &&
      (backend->activate_packet_space == NULL ||
       !backend->activate_packet_space(backend->activation_context,
                                       profile->activation_index,
                                       local_address)))
    return false;

  backend->local_address[profile->set_interface] = local_address;
  backend->valid_interfaces |= UINT32_C(1) << profile->set_interface;
  return true;
}

static bool
set_table_profile(struct npu_wifi_tx_buffer_space_backend *backend,
                  const struct npu_wifi_tx_buffer_space_profile *profile,
                  uint32_t host_address) {
  struct npu_wifi_tx_buffer_space_arena *arena;
  struct npu_wifi_region region;
  uint32_t local_address;
  size_t required_size;

  arena = find_arena(backend, profile->set_interface);
  if (arena == NULL || arena->initialized ||
      !npu_wifi_tx_buffer_space_region_lookup(profile, backend->dynamic_base,
                                              host_address, &region) ||
      arena->physical_base != region.address)
    return false;

  required_size = (size_t)profile->record_count * profile->record_size;
  if (required_size != region.usable_size ||
      arena->record_memory_size < required_size ||
      arena->record_memory_size > region.reserved_size)
    return false;

  if (profile->storage == NPU_WIFI_TX_BUFFER_SPACE_EXTERNAL) {
    if (!an7581_dma_buffer_map(region.address, (uint32_t)required_size,
                               sizeof(uint32_t), &local_address))
      return false;
  } else {
    local_address = region.address;
  }

  if (npu_wifi_tx_buffer_space_initialize(profile, arena->record_memory,
                                          arena->record_memory_size) !=
      NPU_RUNTIME_SUCCESS)
    return false;

  arena->initialized = true;
  backend->local_address[profile->set_interface] = local_address;
  backend->valid_interfaces |= UINT32_C(1) << profile->set_interface;
  return true;
}

static bool set_tx_buffer_space_base(void *context, uint32_t set_interface,
                                     uint32_t host_address) {
  struct npu_wifi_tx_buffer_space_backend *backend = context;
  const struct npu_wifi_tx_buffer_space_profile *profile;

  if (backend == NULL)
    return false;

  profile = npu_wifi_tx_buffer_space_find_profile(set_interface);
  if (profile == NULL)
    return false;
  if (profile->role == NPU_WIFI_TX_BUFFER_SPACE_FREE_POINTER_TABLE)
    return set_table_profile(backend, profile, host_address);
  return set_address_profile(backend, profile, host_address);
}

const struct npu_wifi_backend_operations
    npu_wifi_tx_buffer_space_backend_operations = {
        .set_tx_buffer_space_base = set_tx_buffer_space_base,
};

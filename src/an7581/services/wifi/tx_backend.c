/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_backend.h"

#include "an7581/runtime/memory.h"

static struct npu_wifi_tx_arena *
find_arena(struct npu_wifi_tx_static_backend *backend, uint32_t get_interface) {
  size_t index;

  if (backend == NULL || backend->arenas == NULL)
    return NULL;

  for (index = 0U; index < backend->arena_count; ++index) {
    if ((uint32_t)backend->arenas[index].get_interface == get_interface)
      return &backend->arenas[index];
  }
  return NULL;
}

bool npu_wifi_tx_static_backend_initialize_ring(
    struct npu_wifi_configuration *configuration,
    struct npu_wifi_tx_static_backend *backend, uint32_t get_interface) {
  const struct npu_wifi_tx_ring_profile *profile;
  struct npu_wifi_region region;
  struct npu_wifi_tx_arena *arena;
  size_t required_size;

  if (configuration == NULL || backend == NULL)
    return false;

  profile = npu_wifi_tx_ring_find_profile(get_interface);
  arena = find_arena(backend, get_interface);
  if (profile == NULL || arena == NULL || arena->initialized ||
      !npu_wifi_tx_ring_region_lookup(backend->dynamic_base, get_interface,
                                      &region) ||
      arena->physical_base != region.address)
    return false;

  required_size = (size_t)profile->descriptor_count * profile->descriptor_size;
  if (required_size != region.usable_size ||
      arena->descriptor_memory_size < required_size ||
      arena->descriptor_memory_size > region.reserved_size)
    return false;

  (void)npu_memset(arena->descriptor_memory, 0U, required_size);
  if (!npu_wifi_publish_rx_descriptor_base(configuration, get_interface,
                                           region.address)) {
    (void)npu_memset(arena->descriptor_memory, 0U, required_size);
    return false;
  }

  arena->initialized = true;
  return true;
}

bool npu_wifi_tx_static_backend_prepare_descriptor_base(
    struct npu_wifi_tx_static_backend *backend, uint32_t get_interface) {
  const struct npu_wifi_tx_ring_profile *profile;
  struct npu_wifi_region region;
  struct npu_wifi_tx_arena *arena;

  if (backend == NULL)
    return false;

  profile = npu_wifi_tx_ring_find_profile(get_interface);
  if (profile == NULL)
    return true;
  arena = find_arena(backend, get_interface);
  if (arena == NULL || !arena->initialized ||
      !npu_wifi_tx_ring_region_lookup(backend->dynamic_base, get_interface,
                                      &region) ||
      arena->physical_base != region.address)
    return false;

  return npu_wifi_tx_ring_prime(profile, arena->descriptor_memory,
                                arena->descriptor_memory_size) ==
         NPU_RUNTIME_SUCCESS;
}

bool npu_wifi_tx_static_backend_release_ring(
    struct npu_wifi_configuration *configuration,
    struct npu_wifi_tx_static_backend *backend, uint32_t get_interface) {
  const struct npu_wifi_tx_ring_profile *profile;
  struct npu_wifi_tx_arena *arena;
  size_t required_size;

  if (configuration == NULL || backend == NULL)
    return false;

  profile = npu_wifi_tx_ring_find_profile(get_interface);
  arena = find_arena(backend, get_interface);
  if (profile == NULL || arena == NULL || !arena->initialized)
    return false;

  required_size = (size_t)profile->descriptor_count * profile->descriptor_size;
  (void)npu_memset(arena->descriptor_memory, 0U, required_size);
  if (!npu_wifi_unpublish_rx_descriptor_base(configuration, get_interface))
    return false;

  arena->initialized = false;
  return true;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rx_backend.h"

static struct npu_wifi_rx_arena *
find_arena(struct npu_wifi_rx_static_backend *backend, uint32_t set_interface) {
  size_t index;

  if (backend == NULL || backend->arenas == NULL)
    return NULL;

  for (index = 0U; index < backend->arena_count; ++index) {
    if ((uint32_t)backend->arenas[index].set_interface == set_interface)
      return &backend->arenas[index];
  }
  return NULL;
}

static enum npu_runtime_result initialize_msdu_page_ring(
    struct npu_wifi_rx_arena *arena,
    const struct npu_wifi_rx_ring_profile *profile, uint32_t descriptor_count,
    const struct npu_wifi_rx_buffer_operations *buffer_operations,
    void *buffer_context) {
  struct npu_wifi_msdu_page_descriptor_config config;
  enum npu_runtime_result result;

  if (profile->kind != NPU_WIFI_RX_RING_MSDU_PAGE ||
      buffer_operations == NULL ||
      (arena->msdu_page_state.ready &&
       arena->initialized_descriptor_count !=
           arena->msdu_page_state.descriptor_count) ||
      (!arena->msdu_page_state.ready &&
       arena->initialized_descriptor_count != 0U))
    return NPU_RUNTIME_INVALID_ARGUMENT;

  config = (struct npu_wifi_msdu_page_descriptor_config){
      .descriptor_memory = arena->descriptor_memory,
      .page_ids = arena->buffer_ids,
      .descriptor_memory_size = arena->descriptor_memory_size,
      .page_pool_base = arena->packet_buffer_base,
      .page_id_capacity = arena->buffer_id_capacity,
      .set_interface = arena->set_interface,
      .page_id_operations = *buffer_operations,
      .operation_context = buffer_context,
  };
  result = npu_wifi_msdu_page_descriptors_initialize(&arena->msdu_page_state,
                                                     &config, descriptor_count);
  arena->initialized_descriptor_count =
      arena->msdu_page_state.ready ? arena->msdu_page_state.descriptor_count
                                   : 0U;
  return result;
}

static bool initialize_rx_ring(void *context, uint32_t set_interface,
                               uint32_t descriptor_count,
                               uint32_t *rx_descriptor_base) {
  struct npu_wifi_rx_static_backend *backend = context;
  const struct npu_wifi_rx_ring_profile *profile;
  struct npu_wifi_region region;
  struct npu_wifi_rx_arena *arena;
  const struct npu_wifi_rx_buffer_operations *buffer_operations;
  void *buffer_context;
  enum npu_runtime_result result;

  if (backend == NULL || rx_descriptor_base == NULL)
    return false;

  profile = npu_wifi_rx_ring_find_profile(set_interface);
  arena = find_arena(backend, set_interface);
  if (profile == NULL || arena == NULL ||
      !npu_wifi_rx_ring_region_lookup(backend->dynamic_base, set_interface,
                                      &region) ||
      arena->physical_base != region.address ||
      profile->descriptor_size == 0U ||
      descriptor_count > region.usable_size / profile->descriptor_size ||
      arena->descriptor_memory_size > region.reserved_size)
    return false;

  buffer_operations = arena->buffer_operations != NULL
                          ? arena->buffer_operations
                          : backend->buffer_operations;
  buffer_context = arena->buffer_operations != NULL ? arena->buffer_context
                                                    : backend->buffer_context;
  if (profile->kind == NPU_WIFI_RX_RING_MSDU_PAGE) {
    result = initialize_msdu_page_ring(arena, profile, descriptor_count,
                                       buffer_operations, buffer_context);
  } else {
    if (arena->initialized_descriptor_count != 0U)
      return false;
    result = npu_wifi_rx_ring_initialize(
        profile, arena->packet_buffer_base, arena->descriptor_memory,
        arena->descriptor_memory_size, descriptor_count, arena->buffer_ids,
        arena->buffer_id_capacity, buffer_operations, buffer_context);
  }
  if (result != NPU_RUNTIME_SUCCESS)
    return false;

  arena->initialized_descriptor_count = descriptor_count;
  *rx_descriptor_base = region.address;
  return true;
}

const struct npu_wifi_backend_operations npu_wifi_rx_static_backend_operations =
    {
        .initialize_rx_ring = initialize_rx_ring,
};

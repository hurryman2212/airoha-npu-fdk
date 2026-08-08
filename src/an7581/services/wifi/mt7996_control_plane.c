/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_control_plane.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/wifi_tdm_rx.h"
#include "an7581/runtime/memory.h"

struct rx_arena_layout {
  uint32_t set_interface;
  uint32_t packet_id_offset;
  uint32_t packet_id_capacity;
  bool uses_msdu_page_ids;
};

static const struct rx_arena_layout rx_arena_layouts[] = {
    {NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE, UINT32_C(0x0000),
     NPU_WIFI_RX_DESCRIPTOR_LIMIT, false},
    {NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE, UINT32_C(0x0600),
     NPU_WIFI_RX_MT7996_SECONDARY_DESCRIPTOR_LIMIT, false},
    {NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_INTERFACE, UINT32_C(0x0a00),
     NPU_WIFI_RX_MT7996_MSDU0_DESCRIPTOR_LIMIT, true},
    {NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_INTERFACE, UINT32_C(0x0b00),
     NPU_WIFI_RX_MT7996_MSDU1_DESCRIPTOR_LIMIT, true},
    {NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_INTERFACE, UINT32_C(0x0d00),
     NPU_WIFI_RX_MT7996_MSDU2_DESCRIPTOR_LIMIT, true},
    {NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE, 0U, 0U, false},
};

_Static_assert(sizeof(rx_arena_layouts) / sizeof(rx_arena_layouts[0]) ==
                   NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT,
               "MT7996 RX arena count changed");
_Static_assert(UINT32_C(0x0d00) + NPU_WIFI_RX_MT7996_MSDU2_DESCRIPTOR_LIMIT ==
                   NPU_WIFI_MT7996_CONTROL_PACKET_ID_COUNT,
               "MT7996 packet-ID side-table layout changed");

static bool pointer_is_aligned(const void *pointer, size_t alignment) {
  return pointer != NULL &&
         ((uintptr_t)pointer & (uintptr_t)(alignment - 1U)) == 0U;
}

static bool
binding_matches_region(const struct npu_wifi_mt7996_memory_binding *binding,
                       const struct npu_wifi_region *region, size_t alignment) {
  return binding != NULL && region != NULL &&
         pointer_is_aligned(binding->memory, alignment) &&
         binding->physical_base == region->address &&
         binding->size >= region->usable_size &&
         binding->size <= region->reserved_size;
}

static bool resolve_allocated_regions(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  struct npu_wifi_sram_allocator *allocator = control_plane->allocator_owner;

  if (allocator == NULL)
    return false;
  if (!control_plane->shared_state_external)
    npu_wifi_sram_allocator_reset(allocator);
  if (!npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE,
                                     &control_plane->packet_recycle_region) ||
      !npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING,
                                     &control_plane->token_region) ||
      !npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH,
                                     &control_plane->force_reset_region) ||
      !npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA,
                                     &control_plane->dynamic_region))
    return false;

  return binding_matches_region(&config->packet_recycle,
                                &control_plane->packet_recycle_region,
                                sizeof(uint16_t)) &&
         binding_matches_region(&config->token_ids,
                                &control_plane->token_region,
                                sizeof(uint16_t)) &&
         binding_matches_region(&config->force_reset_ids,
                                &control_plane->force_reset_region,
                                sizeof(uint16_t)) &&
         binding_matches_region(&config->dynamic_arena,
                                &control_plane->dynamic_region,
                                sizeof(uint32_t));
}

static void *
dynamic_region_memory(const struct npu_wifi_mt7996_control_plane_config *config,
                      const struct npu_wifi_region *dynamic_region,
                      const struct npu_wifi_region *region) {
  uint32_t offset;

  if (config == NULL || dynamic_region == NULL || region == NULL ||
      region->address < dynamic_region->address)
    return NULL;

  offset = region->address - dynamic_region->address;
  if ((size_t)offset > config->dynamic_arena.size ||
      region->usable_size > config->dynamic_arena.size - (size_t)offset)
    return NULL;
  return (uint8_t *)config->dynamic_arena.memory + offset;
}

static bool initialize_packet_pool(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  const struct npu_wifi_packet_id_pool_config pool_config = {
      .token_entries = config->token_ids.memory,
      .recycle_entries = config->packet_recycle.memory,
      .acquire = config->acquire,
      .release = config->release,
      .lock_context = config->lock_context,
      .diagnostic_counters = config->diagnostic_counters,
      .token_entry_capacity = NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT,
      .token_entry_count = NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT,
      .recycle_entry_count = NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT,
  };

  if (control_plane->packet_pool_owner == NULL)
    return false;
  if (control_plane->shared_state_external) {
    const struct npu_wifi_packet_id_pool *pool =
        control_plane->packet_pool_owner;

    if (!pool->initialized || pool->token_entries != config->token_ids.memory ||
        pool->recycle_entries != config->packet_recycle.memory ||
        pool->acquire != config->acquire || pool->release != config->release ||
        pool->lock_context != config->lock_context ||
        pool->token_entry_capacity !=
            NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
        pool->token_entry_count < 2U ||
        pool->token_entry_count > pool->token_entry_capacity)
      return false;
  } else if (npu_wifi_packet_id_pool_initialize(
                 control_plane->packet_pool_owner, &pool_config) !=
             NPU_RUNTIME_SUCCESS) {
    return false;
  }

  return npu_wifi_packet_id_backend_initialize(
             &control_plane->packet_id_backend,
             control_plane->packet_pool_owner) == NPU_RUNTIME_SUCCESS;
}

static bool initialize_msdu_page_id_pool(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  struct npu_wifi_region region;

  if (!npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_MSDU_PAGE_ID_MAP, &region) ||
      region.address != UINT32_C(0x3e8a9c70) ||
      config->msdu_page_ids.physical_base != region.address ||
      config->msdu_page_ids.size !=
          NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT * sizeof(uint16_t) ||
      !pointer_is_aligned(config->msdu_page_ids.memory, sizeof(uint16_t)))
    return false;

  return npu_wifi_buffer_id_map_initialize(
             &control_plane->msdu_page_id_pool, config->msdu_page_ids.memory,
             NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT,
             config->diagnostic_counters) == NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result force_reset_tdm_rx_token_pool(void *context) {
  return an7581_wifi_tdm_rx_token_pool_force_reset(context);
}

static bool initialize_tx_done_packet_id_map(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  struct npu_wifi_region region;

  if (!npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_TX_DONE_PACKET_ID_MAP, &region) ||
      region.address != NPU_WIFI_TX_DONE_MT7996_PACKET_ID_MAP_ADDRESS ||
      config->tx_done_packet_ids.physical_base != region.address ||
      config->tx_done_packet_ids.size !=
          NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT * sizeof(uint16_t) ||
      !pointer_is_aligned(config->tx_done_packet_ids.memory,
                          sizeof(uint16_t)) ||
      (config->tdm_rx_platform == NULL) ==
          (config->force_reset_token_ids == NULL))
    return false;

  control_plane->tx_done_packet_ids = config->tx_done_packet_ids.memory;
  if (config->tdm_rx_platform != NULL) {
    control_plane->force_reset_token_ids = force_reset_tdm_rx_token_pool;
    control_plane->force_reset_context = config->tdm_rx_platform;
  } else {
    control_plane->force_reset_token_ids = config->force_reset_token_ids;
    control_plane->force_reset_context = config->force_reset_context;
  }
  return true;
}

static bool initialize_rx_backend(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  size_t index;

  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT; ++index) {
    const struct rx_arena_layout *layout = &rx_arena_layouts[index];
    struct npu_wifi_rx_arena *arena = &control_plane->rx_arenas[index];
    struct npu_wifi_region region;

    if (!npu_wifi_rx_ring_region_lookup(control_plane->dynamic_region.address,
                                        layout->set_interface, &region))
      return false;
    arena->descriptor_memory =
        dynamic_region_memory(config, &control_plane->dynamic_region, &region);
    if (arena->descriptor_memory == NULL)
      return false;
    arena->descriptor_memory_size = region.usable_size;
    arena->physical_base = region.address;
    arena->set_interface = (uint8_t)layout->set_interface;
    if (layout->packet_id_capacity != 0U) {
      arena->buffer_ids = &control_plane->packet_ids[layout->packet_id_offset];
      arena->buffer_id_capacity = layout->packet_id_capacity;
      if (layout->uses_msdu_page_ids) {
        arena->buffer_operations = &npu_wifi_buffer_id_map_operations;
        arena->buffer_context = &control_plane->msdu_page_id_pool;
      }
    }
  }

  control_plane->rx_backend = (struct npu_wifi_rx_static_backend){
      .arenas = control_plane->rx_arenas,
      .buffer_operations = &npu_wifi_packet_id_backend_operations,
      .buffer_context = &control_plane->packet_id_backend,
      .arena_count = NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT,
      .dynamic_base = control_plane->dynamic_region.address,
  };
  return true;
}

static bool initialize_tx_ring_backend(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  static const uint8_t get_interfaces[] = {
      NPU_WIFI_MT7996_TX_FREE_POINTER_BAND0_INTERFACE,
      NPU_WIFI_MT7996_TX_FREE_POINTER_BAND2_INTERFACE,
  };
  size_t index;

  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_TX_ARENA_COUNT; ++index) {
    struct npu_wifi_tx_arena *arena = &control_plane->tx_arenas[index];
    struct npu_wifi_region region;

    if (!npu_wifi_tx_ring_region_lookup(control_plane->dynamic_region.address,
                                        get_interfaces[index], &region))
      return false;
    arena->descriptor_memory =
        dynamic_region_memory(config, &control_plane->dynamic_region, &region);
    if (arena->descriptor_memory == NULL)
      return false;
    arena->descriptor_memory_size = region.usable_size;
    arena->physical_base = region.address;
    arena->get_interface = get_interfaces[index];
  }

  control_plane->tx_backend = (struct npu_wifi_tx_static_backend){
      .arenas = control_plane->tx_arenas,
      .arena_count = NPU_WIFI_MT7996_CONTROL_TX_ARENA_COUNT,
      .dynamic_base = control_plane->dynamic_region.address,
  };
  return true;
}

static bool initialize_tx_buffer_backend(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  static const uint8_t set_interfaces[] = {
      NPU_WIFI_MT7996_TX_FREE_POINTER_BAND0_INTERFACE,
      NPU_WIFI_MT7996_TX_FREE_POINTER_BAND2_INTERFACE,
  };
  static const uint8_t region_types[] = {
      NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_BAND0,
      NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_SECONDARY,
  };
  size_t index;

  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_TX_BUFFER_ARENA_COUNT;
       ++index) {
    struct npu_wifi_tx_buffer_space_arena *arena =
        &control_plane->tx_buffer_arenas[index];
    struct npu_wifi_region region;

    if (!npu_wifi_mt7996_dynamic_region_lookup(
            control_plane->dynamic_region.address, region_types[index],
            &region))
      return false;
    arena->record_memory =
        dynamic_region_memory(config, &control_plane->dynamic_region, &region);
    if (arena->record_memory == NULL)
      return false;
    arena->record_memory_size = region.usable_size;
    arena->physical_base = region.address;
    arena->set_interface = set_interfaces[index];
  }

  control_plane->tx_buffer_backend = (struct npu_wifi_tx_buffer_space_backend){
      .arenas = control_plane->tx_buffer_arenas,
      .activate_packet_space = npu_wifi_tx_packet_static_backend_activate,
      .activation_context = &control_plane->tx_packet_backend,
      .arena_count = NPU_WIFI_MT7996_CONTROL_TX_BUFFER_ARENA_COUNT,
      .dynamic_base = control_plane->dynamic_region.address,
  };
  return true;
}

static bool initialize_tx_packet_backend(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  size_t index;

  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_TX_PACKET_ARENA_COUNT;
       ++index) {
    struct npu_wifi_tx_packet_arena *arena =
        &control_plane->tx_packet_arenas[index];
    struct npu_wifi_region region;

    if (!npu_wifi_tx_packet_space_region_lookup((uint32_t)index, &region) ||
        !binding_matches_region(&config->tx_packet_descriptors[index], &region,
                                sizeof(uint32_t)))
      return false;
    arena->descriptor_memory = config->tx_packet_descriptors[index].memory;
    arena->descriptor_memory_size = config->tx_packet_descriptors[index].size;
    arena->physical_base = region.address;
    arena->activation_index = (uint8_t)index;
  }

  control_plane->tx_packet_backend = (struct npu_wifi_tx_packet_static_backend){
      .arenas = control_plane->tx_packet_arenas,
      .arena_count = NPU_WIFI_MT7996_CONTROL_TX_PACKET_ARENA_COUNT,
  };
  return true;
}

static uint32_t
rx_packet_buffer_base(const struct npu_wifi_mt7996_control_plane *control_plane,
                      const struct npu_wifi_rx_ring_profile *profile) {
  const struct npu_wifi_configuration *configuration =
      control_plane->configuration;

  if (profile->kind == NPU_WIFI_RX_RING_MSDU_PAGE)
    return configuration->dram_ba_node_address_valid
               ? configuration->dram_ba_node_address
               : 0U;
  if (profile->allocates_buffers)
    return configuration->packet_buffer_address_valid
               ? configuration->packet_buffer_address
               : 0U;
  return 0U;
}

static bool initialize_external_tx_done_ring(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_rx_ring_profile *profile, uint32_t descriptor_count,
    uint32_t *rx_descriptor_base) {
  uint32_t packet_buffer_base;
  size_t descriptor_memory_size;
  void *descriptor_memory;
  enum npu_runtime_result status;

  if (!control_plane->tx_done_host_address_valid ||
      control_plane->tx_done_state.ready ||
      (control_plane->tx_done_state.descriptor_count != 0U &&
       (uint32_t)control_plane->tx_done_state.descriptor_count !=
           descriptor_count) ||
      control_plane->map_host_buffer == NULL)
    return false;

  descriptor_memory_size = (size_t)descriptor_count * profile->descriptor_size;
  if (!control_plane->map_host_buffer(
          control_plane->map_context, control_plane->tx_done_host_address,
          descriptor_memory_size, sizeof(uint32_t), &descriptor_memory) ||
      !pointer_is_aligned(descriptor_memory, sizeof(uint32_t)))
    return false;
  if (control_plane->tx_done_descriptor_memory != NULL &&
      (control_plane->tx_done_descriptor_memory != descriptor_memory ||
       control_plane->tx_done_descriptor_memory_size != descriptor_memory_size))
    return false;

  packet_buffer_base = rx_packet_buffer_base(control_plane, profile);
  if (packet_buffer_base == 0U)
    return false;

  const struct npu_wifi_tx_done_descriptor_config descriptor_config = {
      .packet_buffer_base = packet_buffer_base,
      .descriptor_memory = descriptor_memory,
      .descriptor_memory_size = descriptor_memory_size,
      .packet_ids = control_plane->tx_done_packet_ids,
      .packet_id_capacity = NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT,
      .operations =
          {
              .packet_ids = npu_wifi_packet_id_backend_operations,
              .force_reset_token_ids = control_plane->force_reset_token_ids,
          },
      .packet_id_context = &control_plane->packet_id_backend,
      .force_reset_context = control_plane->force_reset_context,
  };
  status = npu_wifi_tx_done_descriptors_initialize(
      &control_plane->tx_done_state, &descriptor_config, descriptor_count);
  if (control_plane->tx_done_state.descriptor_count != 0U) {
    control_plane->tx_done_descriptor_memory = descriptor_memory;
    control_plane->tx_done_descriptor_memory_size = descriptor_memory_size;
  }
  if (status != NPU_RUNTIME_SUCCESS)
    return false;

  *rx_descriptor_base =
      control_plane->tx_done_host_address & UINT32_C(0x1fffffff);
  return true;
}

static bool initialize_rx_ring(void *context, uint32_t set_interface,
                               uint32_t descriptor_count,
                               uint32_t *rx_descriptor_base) {
  struct npu_wifi_mt7996_control_plane *control_plane = context;
  const struct npu_wifi_rx_ring_profile *profile;
  struct npu_wifi_rx_arena *arena;
  size_t index;
  uint32_t packet_buffer_base;

  if (control_plane == NULL || !control_plane->initialized ||
      rx_descriptor_base == NULL)
    return false;

  profile = npu_wifi_rx_ring_find_profile(set_interface);
  if (profile == NULL)
    return false;
  if (profile->kind == NPU_WIFI_RX_RING_TX_DONE)
    return initialize_external_tx_done_ring(
        control_plane, profile, descriptor_count, rx_descriptor_base);

  packet_buffer_base = rx_packet_buffer_base(control_plane, profile);
  if (profile->allocates_buffers && packet_buffer_base == 0U)
    return false;
  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT; ++index) {
    arena = &control_plane->rx_arenas[index];
    if ((uint32_t)arena->set_interface == set_interface) {
      arena->packet_buffer_base = packet_buffer_base;
      return npu_wifi_rx_static_backend_operations.initialize_rx_ring(
          &control_plane->rx_backend, set_interface, descriptor_count,
          rx_descriptor_base);
    }
  }
  return false;
}

static bool set_tx_done_ring_base(void *context, uint32_t set_interface,
                                  uint32_t host_address) {
  struct npu_wifi_mt7996_control_plane *control_plane = context;

  if (control_plane == NULL || !control_plane->initialized ||
      set_interface != NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_INTERFACE ||
      host_address == 0U ||
      /* Host DMA addresses are limited to the recovered 30-bit window. */
      host_address > UINT32_C(0xbfffffff) ||
      (host_address & (sizeof(uint32_t) - 1U)) != 0U)
    return false;
  if (control_plane->tx_done_host_address_valid)
    return control_plane->tx_done_host_address == host_address;
  if (control_plane->tx_done_state.descriptor_count != 0U)
    return false;

  control_plane->tx_done_host_address = host_address;
  control_plane->tx_done_host_address_valid = true;
  return true;
}

static bool prepare_rx_descriptor_base(void *context, uint32_t get_interface) {
  struct npu_wifi_mt7996_control_plane *control_plane = context;

  if (control_plane == NULL || !control_plane->initialized)
    return false;
  return npu_wifi_tx_static_backend_prepare_descriptor_base(
      &control_plane->tx_backend, get_interface);
}

static bool set_tx_descriptor_base(void *context, uint32_t set_interface,
                                   uint32_t physical_address) {
  struct npu_wifi_mt7996_control_plane *control_plane = context;

  if (control_plane == NULL || !control_plane->initialized)
    return false;

  return npu_wifi_tx_ring_set_descriptor_base_sram(
      &control_plane->tx_descriptor_base, physical_address, set_interface,
      control_plane->report_tx_descriptor_sram_warning,
      control_plane->tx_descriptor_warning_context);
}

static bool set_tx_buffer_space_base(void *context, uint32_t set_interface,
                                     uint32_t host_address) {
  struct npu_wifi_mt7996_control_plane *control_plane = context;

  if (control_plane == NULL || !control_plane->initialized)
    return false;

  return npu_wifi_tx_buffer_space_backend_operations.set_tx_buffer_space_base(
      &control_plane->tx_buffer_backend, set_interface, host_address);
}

static const struct npu_wifi_backend_operations control_operations = {
    .initialize_rx_ring = initialize_rx_ring,
    .prepare_rx_descriptor_base = prepare_rx_descriptor_base,
    .set_tx_descriptor_base = set_tx_descriptor_base,
    .set_tx_buffer_space_base = set_tx_buffer_space_base,
    .set_tx_done_ring_base = set_tx_done_ring_base,
};

static bool initialize_eagle_tx_backend(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  if (config->read32 == NULL)
    return npu_wifi_eagle_tx_backend_initialize(
        &control_plane->eagle_tx_backend, control_plane->dynamic_region.address,
        config->write32, config->write_context);

  return npu_wifi_eagle_tx_backend_initialize_verified(
      &control_plane->eagle_tx_backend, control_plane->dynamic_region.address,
      config->write32, config->write_context, config->read32,
      config->read_context);
}

static bool initialize_backend_bundle(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  struct npu_wifi_backend_binding
      components[3U + NPU_WIFI_MT7996_CONTROL_ADDITIONAL_BACKEND_LIMIT];
  size_t component_count = 0U;
  size_t index;

  components[component_count++] = (struct npu_wifi_backend_binding){
      .operations = &control_operations,
      .context = control_plane,
  };
  components[component_count++] = (struct npu_wifi_backend_binding){
      .operations = &npu_wifi_rx_pcie_backend_operations,
      .context = &control_plane->rx_pcie_backend,
  };
  components[component_count++] = (struct npu_wifi_backend_binding){
      .operations = &npu_wifi_eagle_tx_backend_operations,
      .context = &control_plane->eagle_tx_backend,
  };
  for (index = 0U; index < config->additional_backend_count; ++index)
    components[component_count++] = config->additional_backends[index];

  return npu_wifi_rx_pcie_backend_initialize(
             &control_plane->rx_pcie_backend, config->configuration,
             config->write32, config->write_context) &&
         initialize_eagle_tx_backend(control_plane, config) &&
         npu_wifi_backend_bundle_initialize(&control_plane->backend_bundle,
                                            components, component_count);
}

static bool
publish_cold_tx_rings(struct npu_wifi_mt7996_control_plane *control_plane) {
  if (!npu_wifi_tx_static_backend_initialize_ring(
          control_plane->configuration, &control_plane->tx_backend,
          NPU_WIFI_MT7996_TX_FREE_POINTER_BAND0_INTERFACE))
    return false;
  if (npu_wifi_tx_static_backend_initialize_ring(
          control_plane->configuration, &control_plane->tx_backend,
          NPU_WIFI_MT7996_TX_FREE_POINTER_BAND2_INTERFACE))
    return true;

  (void)npu_wifi_tx_static_backend_release_ring(
      control_plane->configuration, &control_plane->tx_backend,
      NPU_WIFI_MT7996_TX_FREE_POINTER_BAND0_INTERFACE);
  return false;
}

static bool
replay_pcie_configuration(struct npu_wifi_mt7996_control_plane *control_plane) {
  static const uint8_t interfaces[] = {
      NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE,
      NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE,
      NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_INTERFACE,
      NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_INTERFACE,
      NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_INTERFACE,
      NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE,
      NPU_WIFI_MT7996_TX_DONE_REGISTER_INTERFACE,
      NPU_WIFI_MT7996_RRO_CPU_INDEX_INTERFACE,
  };
  const struct npu_wifi_backend_operations *operations =
      &npu_wifi_backend_bundle_operations;
  size_t index;

  for (index = 0U; index < sizeof(interfaces) / sizeof(interfaces[0]);
       ++index) {
    const uint32_t interface = interfaces[index];
    const struct npu_wifi_interface_configuration *configuration =
        &control_plane->configuration->interface[interface];

    if ((configuration->valid_fields & NPU_WIFI_VALID_PCIE_ADDRESS) != 0U &&
        !operations->set_pcie_address(&control_plane->backend_bundle, interface,
                                      configuration->pcie_address))
      return false;
  }
  return true;
}

static bool
replay_tx_configuration(struct npu_wifi_mt7996_control_plane *control_plane) {
  const struct npu_wifi_backend_operations *operations =
      &npu_wifi_backend_bundle_operations;
  struct npu_wifi_configuration *configuration = control_plane->configuration;
  uint32_t interface;

  if (configuration->pcie_port_type_valid &&
      !operations->set_pcie_port_type(&control_plane->backend_bundle,
                                      configuration->pcie_port_type))
    return false;
  for (interface = 0U; interface < NPU_WIFI_INTERFACE_COUNT; ++interface) {
    const struct npu_wifi_interface_configuration *entry =
        &configuration->interface[interface];

    if ((entry->valid_fields & NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS) != 0U &&
        !operations->set_tx_ring_pcie_address(&control_plane->backend_bundle,
                                              interface,
                                              entry->tx_ring_pcie_address))
      return false;
    if ((entry->valid_fields & NPU_WIFI_VALID_TX_DESCRIPTOR_BASE) != 0U &&
        !operations->set_tx_descriptor_base(&control_plane->backend_bundle,
                                            interface,
                                            entry->tx_descriptor_base))
      return false;
    if ((entry->valid_fields & NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE) != 0U &&
        !operations->set_tx_buffer_space_base(&control_plane->backend_bundle,
                                              interface,
                                              entry->tx_buffer_space_base))
      return false;
    if ((entry->valid_fields & NPU_WIFI_VALID_TX_DONE_RING_BASE) != 0U &&
        !operations->set_tx_done_ring_base(&control_plane->backend_bundle,
                                           interface, entry->tx_done_ring_base))
      return false;
  }
  return true;
}

static bool
replay_rx_descriptors(struct npu_wifi_mt7996_control_plane *control_plane) {
  static const uint8_t interfaces[] = {
      NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE,
      NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE,
      NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_INTERFACE,
      NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_INTERFACE,
      NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_INTERFACE,
      NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE,
      NPU_WIFI_MT7996_RX_TX_DONE_INTERFACE,
  };
  size_t index;

  for (index = 0U; index < sizeof(interfaces) / sizeof(interfaces[0]);
       ++index) {
    const uint32_t interface = interfaces[index];
    struct npu_wifi_interface_configuration *configuration =
        &control_plane->configuration->interface[interface];
    const struct npu_wifi_rx_ring_profile *profile =
        npu_wifi_rx_ring_find_profile(interface);
    uint32_t descriptor_base;

    if ((configuration->valid_fields & NPU_WIFI_VALID_DESCRIPTOR_COUNT) == 0U)
      continue;
    if (profile == NULL ||
        !initialize_rx_ring(control_plane, interface,
                            configuration->descriptor_count, &descriptor_base))
      return false;
    if (profile->publication_interface !=
            NPU_WIFI_RX_NO_PUBLICATION_INTERFACE &&
        !npu_wifi_publish_rx_descriptor_base(control_plane->configuration,
                                             profile->publication_interface,
                                             descriptor_base))
      return false;
  }
  return true;
}

static bool
replay_rro_configuration(struct npu_wifi_mt7996_control_plane *control_plane) {
  const struct npu_wifi_backend_operations *operations =
      &npu_wifi_backend_bundle_operations;
  struct npu_wifi_configuration *configuration = control_plane->configuration;
  uint32_t group;
  uint32_t interface;

  if (!operations->set_force_to_cpu(&control_plane->backend_bundle,
                                    configuration->force_to_cpu))
    return false;
  if (configuration->dram_ba_node_address_valid &&
      !operations->set_dram_ba_node_address(
          &control_plane->backend_bundle, configuration->dram_ba_node_address))
    return false;

  for (interface = 0U; interface < NPU_WIFI_INTERFACE_COUNT; ++interface) {
    const struct npu_wifi_interface_configuration *entry =
        &configuration->interface[interface];

    if ((entry->valid_fields & NPU_WIFI_VALID_DELETE_STATION) != 0U &&
        !operations->set_delete_station(&control_plane->backend_bundle,
                                        interface, entry->delete_station))
      return false;
  }

  for (group = 0U; group < NPU_WIFI_INODE_NORMAL_TABLE_GROUP_LIMIT; ++group) {
    const uint32_t validity =
        configuration->inode_pending.normal_table_valid[group / 32U];
    const struct npu_wifi_inode_registers registers = {
        .direction = group,
        .input_count_address =
            configuration->inode_pending.normal_table_address[group],
    };

    if ((validity & (UINT32_C(1) << (group % 32U))) != 0U &&
        !operations->set_inode_txrx_registers(&control_plane->backend_bundle,
                                              0U, &registers))
      return false;
  }
  if (configuration->inode_pending.special_table_valid &&
      !operations->set_inode_txrx_registers(
          &control_plane->backend_bundle, 1U,
          &configuration->inode_pending.special_table))
    return false;

  (void)npu_memset(configuration->inode_pending.normal_table_valid, 0U,
                   sizeof(configuration->inode_pending.normal_table_valid));
  configuration->inode_pending.special_table_valid = false;
  return true;
}

static bool replay_mailbox_configuration(
    struct npu_wifi_mt7996_control_plane *control_plane) {
  return replay_tx_configuration(control_plane) &&
         replay_pcie_configuration(control_plane) &&
         replay_rx_descriptors(control_plane) &&
         (control_plane->backend_bundle.inode_txrx_registers.operations ==
              NULL ||
          replay_rro_configuration(control_plane));
}

enum npu_runtime_result npu_wifi_mt7996_control_plane_initialize(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config) {
  if (control_plane == NULL || config == NULL || config->configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(control_plane, 0U, sizeof(*control_plane));
  control_plane->configuration = config->configuration;
  control_plane->shared_state_external = config->shared_packet_pool != NULL;
  control_plane->allocator_owner = control_plane->shared_state_external
                                       ? config->shared_allocator
                                       : &control_plane->allocator;
  control_plane->packet_pool_owner = control_plane->shared_state_external
                                         ? config->shared_packet_pool
                                         : &control_plane->packet_pool;
  if (!config->activation_allowed) {
    control_plane->activation_gated = true;
    return NPU_RUNTIME_REJECTED;
  }
  if (config->configuration->backend != NULL || config->acquire == NULL ||
      config->release == NULL || config->map_host_buffer == NULL ||
      config->write32 == NULL ||
      (config->shared_allocator == NULL) !=
          (config->shared_packet_pool == NULL) ||
      config->additional_backend_count >
          NPU_WIFI_MT7996_CONTROL_ADDITIONAL_BACKEND_LIMIT ||
      (config->additional_backend_count != 0U &&
       config->additional_backends == NULL))
    return NPU_RUNTIME_OUT_OF_RANGE;

  control_plane->map_host_buffer = config->map_host_buffer;
  control_plane->map_context = config->map_context;
  control_plane->report_tx_descriptor_sram_warning =
      config->report_tx_descriptor_sram_warning;
  control_plane->tx_descriptor_warning_context =
      config->tx_descriptor_warning_context;
  if (!initialize_tx_done_packet_id_map(control_plane, config) ||
      !resolve_allocated_regions(control_plane, config) ||
      !initialize_tx_packet_backend(control_plane, config) ||
      !initialize_rx_backend(control_plane, config) ||
      !initialize_tx_ring_backend(control_plane, config) ||
      !initialize_tx_buffer_backend(control_plane, config) ||
      !initialize_backend_bundle(control_plane, config) ||
      !initialize_packet_pool(control_plane, config) ||
      !initialize_msdu_page_id_pool(control_plane, config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  control_plane->initialized = true;
  if (!publish_cold_tx_rings(control_plane) ||
      !replay_mailbox_configuration(control_plane)) {
    control_plane->initialized = false;
    return NPU_RUNTIME_OUT_OF_RANGE;
  }

  npu_wifi_configuration_set_backend(control_plane->configuration,
                                     &npu_wifi_backend_bundle_operations,
                                     &control_plane->backend_bundle);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_mt7996_control_plane_bind_backends(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_backend_binding *backends, size_t backend_count) {
  if (control_plane == NULL || backends == NULL || backend_count == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!control_plane->initialized || control_plane->configuration == NULL ||
      control_plane->configuration->backend !=
          &npu_wifi_backend_bundle_operations ||
      control_plane->configuration->backend_context !=
          &control_plane->backend_bundle)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  if (!control_plane->additional_backends_bound) {
    if (!npu_wifi_backend_bundle_bind(&control_plane->backend_bundle, backends,
                                      backend_count))
      return NPU_RUNTIME_OUT_OF_RANGE;
    control_plane->additional_backends_bound = true;
  }

  return replay_rro_configuration(control_plane) ? NPU_RUNTIME_SUCCESS
                                                 : NPU_RUNTIME_OUT_OF_RANGE;
}

enum npu_runtime_result npu_wifi_mt7996_control_plane_prepare_reinitialization(
    struct npu_wifi_mt7996_control_plane *control_plane) {
  size_t index;

  if (control_plane == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!control_plane->initialized || control_plane->configuration == NULL)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT; ++index) {
    struct npu_wifi_rx_arena *arena = &control_plane->rx_arenas[index];

    arena->initialized_descriptor_count = 0U;
    (void)npu_memset(&arena->msdu_page_state, 0U,
                     sizeof(arena->msdu_page_state));
  }
  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_TX_BUFFER_ARENA_COUNT;
       ++index)
    control_plane->tx_buffer_arenas[index].initialized = false;
  for (index = 0U; index < NPU_WIFI_MT7996_CONTROL_TX_PACKET_ARENA_COUNT;
       ++index)
    control_plane->tx_packet_arenas[index].initialized = false;

  (void)npu_memset(control_plane->tx_buffer_backend.local_address, 0U,
                   sizeof(control_plane->tx_buffer_backend.local_address));
  control_plane->tx_buffer_backend.valid_interfaces = 0U;
  (void)npu_memset(&control_plane->rx_pcie_backend.state, 0U,
                   sizeof(control_plane->rx_pcie_backend.state));
  (void)npu_memset(&control_plane->tx_done_state, 0U,
                   sizeof(control_plane->tx_done_state));
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

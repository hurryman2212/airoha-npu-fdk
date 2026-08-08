/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_tdm_rx.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool
memory_configuration_is_valid(const struct an7581_wifi_tdm_rx_memory *memory) {
  uint32_t ring_index;

  if (memory == NULL || memory->reset_scratch_entries == NULL ||
      memory->token_states == NULL || memory->global_control == NULL ||
      memory->global_ring_enable == NULL ||
      !pointer_is_aligned(memory->reset_scratch_entries, sizeof(uint16_t)) ||
      !pointer_is_aligned(memory->token_states, sizeof(uint16_t)) ||
      !pointer_is_aligned(memory->global_control, sizeof(uint32_t)) ||
      !pointer_is_aligned(memory->global_ring_enable, sizeof(uint32_t)))
    return false;

  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    if (memory->descriptors[ring_index] == NULL ||
        memory->registers[ring_index] == NULL ||
        !pointer_is_aligned(memory->descriptors[ring_index],
                            sizeof(uint32_t)) ||
        !pointer_is_aligned(memory->registers[ring_index], sizeof(uint32_t)) ||
        (memory->descriptor_physical_base[ring_index] &
         (NPU_WIFI_TDM_RX_DESCRIPTOR_SIZE - 1U)) != 0U)
      return false;
  }
  return true;
}

enum npu_runtime_result an7581_wifi_tdm_rx_memory_resolve(
    struct npu_wifi_sram_allocator *allocator,
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_tdm_rx_memory *memory) {
  struct npu_wifi_region descriptor_region;
  struct npu_wifi_region scratch_region;
  uint32_t ring_index;
  uint32_t token_state_address;

  if (allocator == NULL || configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!configuration->tx_packet_buffer_address_valid ||
      configuration->tx_packet_buffer_address_out_of_range ||
      !configuration->tx_buffer_check_address_valid ||
      !configuration->token_id_size_valid ||
      configuration->token_id_size <= NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT ||
      configuration->token_id_size >
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      !npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_FIXED_TDM_RX_DESCRIPTORS,
                                     &descriptor_region) ||
      descriptor_region.address != AN7581_WIFI_TDM_RX_DESCRIPTOR_BASE ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH, &scratch_region) ||
      scratch_region.usable_size !=
          NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT * sizeof(uint16_t) ||
      !an7581_dma_buffer_map(configuration->tx_buffer_check_address,
                             configuration->token_id_size * sizeof(uint16_t),
                             sizeof(uint16_t), &token_state_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(memory, 0U, sizeof(*memory));
  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    memory->descriptor_physical_base[ring_index] =
        descriptor_region.address + ring_index * AN7581_WIFI_TDM_RX_RING_SPAN;
    memory->descriptors[ring_index] =
        (volatile struct an7581_qdma_descriptor *)(uintptr_t)
            memory->descriptor_physical_base[ring_index];
    memory->registers[ring_index] =
        (volatile struct npu_wifi_tdm_rx_registers
             *)(uintptr_t)(AN7581_WIFI_TDM_RX_REGISTERS_BASE +
                           ring_index * AN7581_WIFI_TDM_RX_REGISTER_STRIDE);
  }
  memory->reset_scratch_entries =
      (volatile uint16_t *)(uintptr_t)scratch_region.address;
  memory->token_states = (volatile uint16_t *)(uintptr_t)token_state_address;
  memory->global_control =
      (volatile uint32_t *)(uintptr_t)AN7581_WIFI_TDM_RX_GLOBAL_CONTROL_ADDRESS;
  memory->global_ring_enable =
      (volatile uint32_t *)(uintptr_t)AN7581_WIFI_TDM_RX_GLOBAL_ENABLE_ADDRESS;
  memory->packet_buffer_base = configuration->tx_packet_buffer_address;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_tdm_rx_platform_initialize(
    struct an7581_wifi_tdm_rx_platform *platform,
    const struct an7581_wifi_tdm_rx_platform_config *config) {
  struct npu_wifi_tdm_rx_config receiver_config;
  uint32_t ring_index;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!memory_configuration_is_valid(&config->memory) ||
      config->token_pool == NULL || !config->token_pool->initialized ||
      config->dispatch == NULL ||
      config->token_state_count < config->token_pool->token_entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  (void)npu_memset(&receiver_config, 0U, sizeof(receiver_config));
  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    receiver_config.descriptors[ring_index] =
        config->memory.descriptors[ring_index];
    receiver_config.registers[ring_index] =
        config->memory.registers[ring_index];
    receiver_config.descriptor_physical_base[ring_index] =
        config->memory.descriptor_physical_base[ring_index];
    receiver_config.diagnostic_counters[ring_index] =
        config->diagnostic_counters[ring_index];
    platform->reset.tdm_rings[ring_index] =
        config->memory.descriptors[ring_index];
  }
  receiver_config.global_control = config->memory.global_control;
  receiver_config.global_ring_enable = config->memory.global_ring_enable;
  receiver_config.token_pool = config->token_pool;
  receiver_config.dispatch = config->dispatch;
  receiver_config.publish_dispatch = config->publish_dispatch;
  receiver_config.dispatch_context = config->dispatch_context;
  receiver_config.packet_buffer_base = config->memory.packet_buffer_base;
  status = npu_wifi_tdm_rx_initialize(&platform->receiver, &receiver_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->reset.scratch_entries = config->memory.reset_scratch_entries;
  platform->reset.token_states = config->memory.token_states;
  platform->reset.packet_buffer_base = config->memory.packet_buffer_base;
  platform->reset.scratch_entry_count = NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT;
  platform->reset.token_state_count = config->token_state_count;
  platform->token_state_count = config->token_state_count;
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_tdm_rx_token_pool_force_reset(
    struct an7581_wifi_tdm_rx_platform *platform) {
  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_REJECTED;
  return npu_wifi_token_id_pool_force_reset(platform->receiver.token_pool,
                                            &platform->reset);
}

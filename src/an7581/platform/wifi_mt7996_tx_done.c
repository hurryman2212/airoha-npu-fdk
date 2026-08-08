/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_tx_done.h"

#include "an7581/arch/riscv/cache.h"
#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/region.h"

#define AN7581_WIFI_MT7996_TX_DONE_DEVICE_ALIAS UINT32_C(0x80000000)

static enum npu_runtime_result platform_allocate_packet(void *context,
                                                        uint16_t *packet_id) {
  struct an7581_wifi_mt7996_tx_done_platform *platform = context;

  if (platform == NULL || platform->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_packet_id_pool_allocate(platform->packet_pool, packet_id);
}

static enum npu_runtime_result platform_release_packet(void *context,
                                                       uint16_t packet_id) {
  struct an7581_wifi_mt7996_tx_done_platform *platform = context;

  if (platform == NULL || platform->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_packet_id_pool_release(platform->packet_pool, packet_id);
}

static enum npu_runtime_result platform_release_token(void *context,
                                                      uint16_t token_id) {
  struct an7581_wifi_mt7996_tx_done_platform *platform = context;

  if (platform == NULL || platform->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_token_id_pool_release(platform->packet_pool, token_id);
}

static enum npu_runtime_result platform_enqueue_packet(
    void *context,
    const struct npu_wifi_mt7996_tx_done_completion *completion) {
  struct an7581_wifi_mt7996_tx_done_platform *platform = context;

  if (platform == NULL || platform->enqueue == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return platform->enqueue(platform->enqueue_context, completion);
}

static void platform_discard_cache_line(void *context,
                                        uint32_t device_address) {
  (void)context;
  an7581_l1_dcache_discard((const void *)(uintptr_t)device_address);
}

enum npu_runtime_result an7581_wifi_mt7996_tx_done_memory_resolve(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_tx_done_memory *memory) {
  const struct npu_wifi_interface_configuration *descriptor_interface;
  const struct npu_wifi_interface_configuration *register_interface;
  struct an7581_wifi_mt7996_tx_done_memory candidate;
  struct npu_wifi_region packet_id_region;
  uint32_t descriptor_address;
  uint32_t packet_cached_address;
  uint32_t packet_span = NPU_WIFI_MT7996_TX_DONE_PACKET_ID_LIMIT *
                         NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE;

  if (configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  descriptor_interface =
      &configuration->interface[NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_INTERFACE];
  register_interface =
      &configuration->interface[NPU_WIFI_MT7996_TX_DONE_REGISTER_INTERFACE];
  if (!configuration->packet_buffer_address_valid ||
      !configuration->token_id_size_valid ||
      configuration->token_id_size == 0U ||
      configuration->token_id_size >
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      (descriptor_interface->valid_fields & NPU_WIFI_VALID_TX_DONE_RING_BASE) ==
          0U ||
      (register_interface->valid_fields &
       (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT)) !=
          (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT) ||
      register_interface->descriptor_count == 0U ||
      register_interface->descriptor_count >
          NPU_WIFI_MT7996_TX_DONE_RING_LIMIT ||
      register_interface->pcie_address == 0U ||
      (register_interface->pcie_address & (sizeof(uint32_t) - 1U)) != 0U ||
      register_interface->pcie_address >
          UINT32_MAX - sizeof(struct npu_wifi_tx_ring_registers) ||
      !an7581_dma_buffer_map(descriptor_interface->tx_done_ring_base,
                             register_interface->descriptor_count *
                                 NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_SIZE,
                             sizeof(uint32_t), &descriptor_address) ||
      !an7581_dma_buffer_map(configuration->packet_buffer_address, packet_span,
                             NPU_WIFI_MT7996_TX_DONE_PACKET_SIZE,
                             &packet_cached_address) ||
      !npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_TX_DONE_PACKET_ID_MAP, &packet_id_region) ||
      packet_id_region.address == 0U ||
      (packet_id_region.address & (sizeof(uint16_t) - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  candidate.descriptors =
      (volatile struct npu_wifi_mt7996_tx_done_descriptor *)(uintptr_t)
          descriptor_address;
  candidate.packet_ids =
      (volatile uint16_t *)(uintptr_t)packet_id_region.address;
  candidate.packet_device_memory =
      (volatile uint8_t *)(uintptr_t)((configuration->packet_buffer_address &
                                       AN7581_DMA_PHYSICAL_MASK) |
                                      AN7581_WIFI_MT7996_TX_DONE_DEVICE_ALIAS);
  candidate.packet_cached_memory =
      (volatile uint8_t *)(uintptr_t)packet_cached_address;
  candidate.registers =
      (volatile struct npu_wifi_tx_ring_registers *)(uintptr_t)
          register_interface->pcie_address;
  candidate.descriptor_memory_size = register_interface->descriptor_count *
                                     NPU_WIFI_MT7996_TX_DONE_DESCRIPTOR_SIZE;
  candidate.packet_id_memory_size =
      AN7581_WIFI_MT7996_TX_DONE_PACKET_ID_REGION_SIZE;
  candidate.packet_memory_size = packet_span;
  candidate.packet_physical_base = configuration->packet_buffer_address;
  candidate.ring_count = register_interface->descriptor_count;
  candidate.packet_id_limit = NPU_WIFI_MT7996_TX_DONE_PACKET_ID_LIMIT;
  candidate.active_token_count = configuration->token_id_size;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_tx_done_platform_initialize(
    struct an7581_wifi_mt7996_tx_done_platform *platform,
    const struct an7581_wifi_mt7996_tx_done_config *config) {
  struct npu_wifi_mt7996_tx_done_config service_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->packet_pool == NULL || !config->packet_pool->initialized ||
      config->packet_pool->token_entry_count !=
          config->memory.active_token_count ||
      config->enqueue == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->packet_pool = config->packet_pool;
  platform->enqueue = config->enqueue;
  platform->enqueue_context = config->enqueue_context;
  service_config = (struct npu_wifi_mt7996_tx_done_config){
      .descriptors = config->memory.descriptors,
      .packet_ids = config->memory.packet_ids,
      .packet_device_memory = config->memory.packet_device_memory,
      .packet_cached_memory = config->memory.packet_cached_memory,
      .registers = config->memory.registers,
      .operations =
          {
              .allocate_packet = platform_allocate_packet,
              .release_packet = platform_release_packet,
              .release_token = platform_release_token,
              .enqueue_packet = platform_enqueue_packet,
              .discard_cache_line = platform_discard_cache_line,
          },
      .operation_context = platform,
      .diagnostic_counters = config->packet_pool->diagnostic_counters,
      .records_processed_counter = config->records_processed_counter,
      .invalid_record_type_counter = config->invalid_record_type_counter,
      .descriptor_memory_size = config->memory.descriptor_memory_size,
      .packet_id_memory_size = config->memory.packet_id_memory_size,
      .packet_memory_size = config->memory.packet_memory_size,
      .packet_physical_base = config->memory.packet_physical_base,
      .ring_count = config->memory.ring_count,
      .packet_id_limit = config->memory.packet_id_limit,
      .active_token_count = config->memory.active_token_count,
  };
  status =
      npu_wifi_mt7996_tx_done_initialize(&platform->service, &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

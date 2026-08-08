/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_tdma_delivery.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/region.h"

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

bool an7581_wifi_mt7996_tdma_delivery_memory_is_valid(
    const struct an7581_wifi_mt7996_tdma_delivery_memory *memory) {
  if (memory == NULL)
    return false;
  return memory->descriptors != NULL && memory->registers != NULL &&
         memory->band_group != NULL && memory->band_enable != NULL &&
         memory->global_enable != NULL &&
         pointer_is_aligned(memory->descriptors, sizeof(uint32_t)) &&
         pointer_is_aligned(memory->registers, sizeof(uint32_t)) &&
         pointer_is_aligned(memory->band_group, sizeof(uint32_t)) &&
         pointer_is_aligned(memory->band_enable, sizeof(uint32_t)) &&
         pointer_is_aligned(memory->global_enable, sizeof(uint32_t)) &&
         memory->descriptor_memory_size >=
             AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_MEMORY_SIZE &&
         memory->descriptor_physical_base ==
             AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_BASE;
}

enum npu_runtime_result an7581_wifi_mt7996_tdma_delivery_memory_resolve(
    struct an7581_wifi_mt7996_tdma_delivery_memory *memory) {
  struct npu_wifi_region descriptor_region;

  if (memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_TDMA_DELIVERY_DESCRIPTORS,
          &descriptor_region) ||
      descriptor_region.address !=
          AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_BASE)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *memory = (struct an7581_wifi_mt7996_tdma_delivery_memory){
      .descriptors = (volatile struct an7581_qdma_descriptor *)(uintptr_t)
                         descriptor_region.address,
      .registers = (volatile struct npu_wifi_tx_ring_registers *)(uintptr_t)
          AN7581_WIFI_MT7996_TDMA_DELIVERY_REGISTERS_ADDRESS,
      .band_group = (volatile uint32_t *)(uintptr_t)
          AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_GROUP_ADDRESS,
      .band_enable = (volatile uint32_t *)(uintptr_t)
          AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_ENABLE_ADDRESS,
      .global_enable = (volatile uint32_t *)(uintptr_t)
          AN7581_WIFI_MT7996_TDMA_DELIVERY_GLOBAL_ENABLE_ADDRESS,
      .descriptor_memory_size =
          AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_MEMORY_SIZE,
      .descriptor_physical_base = descriptor_region.address,
  };
  return NPU_RUNTIME_SUCCESS;
}

static void
initialize_descriptors(volatile struct an7581_qdma_descriptor *descriptors) {
  uint32_t index;

  for (index = 0U; index < NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT; ++index) {
    descriptors[index].message[1] =
        AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_TEMPLATE;
    descriptors[index].control =
        (descriptors[index].control & UINT32_C(0x3fffffff)) |
        NPU_WIFI_TX_DESCRIPTOR_READY;
  }
}

static void
publish_hardware(struct an7581_wifi_mt7996_tdma_delivery_platform *platform) {
  const struct an7581_wifi_mt7996_tdma_delivery_memory *memory =
      &platform->memory;

  initialize_descriptors(memory->descriptors);
  an7581_dma_memory_barrier();
  memory->registers->descriptor_base =
      memory->descriptor_physical_base &
      AN7581_WIFI_MT7996_TDMA_DELIVERY_DESCRIPTOR_BASE_MASK;
  memory->registers->descriptor_count = NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT;
  memory->registers->cpu_index = 0U;
  *memory->band_group = AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_GROUP_VALUE;
  *memory->band_enable = AN7581_WIFI_MT7996_TDMA_DELIVERY_BAND_ENABLE_VALUE;
  *memory->global_enable |= AN7581_WIFI_MT7996_TDMA_DELIVERY_GLOBAL_ENABLE_MASK;
  an7581_dma_memory_barrier();
  platform->hardware_published = true;
}

enum npu_runtime_result an7581_wifi_mt7996_tdma_delivery_platform_initialize(
    struct an7581_wifi_mt7996_tdma_delivery_platform *platform,
    const struct an7581_wifi_mt7996_tdma_delivery_config *config) {
  struct npu_wifi_mt7996_tdma_delivery_config delivery_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!an7581_wifi_mt7996_tdma_delivery_memory_is_valid(&config->memory))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->memory = config->memory;
  delivery_config = (struct npu_wifi_mt7996_tdma_delivery_config){
      .band =
          {
              {
                  .descriptors = config->memory.descriptors,
                  .registers = config->memory.registers,
                  .full_observation_counter =
                      config->diagnostic_counters != NULL
                          ? &config->diagnostic_counters->tdma_full_observations
                          : NULL,
                  .capacity_timeout_counter =
                      config->diagnostic_counters != NULL
                          ? &config->diagnostic_counters->tdma_capacity_timeouts
                          : NULL,
                  .published_counter = config->diagnostic_counters != NULL
                                           ? &config->diagnostic_counters
                                                  ->tdma_descriptors_published
                                           : NULL,
                  .producer = 0U,
              },
          },
      .rro_packet_mapping = config->packet_mapping,
      .enqueue = config->enqueue,
      .release = config->release,
      .packet_context = config->packet_context,
      .rro_packet_mapping_size = config->packet_mapping_size,
      .rro_packet_dma_base = config->packet_dma_base,
      .rro_packet_count = config->packet_count,
      .enabled_band_mask = UINT32_C(1),
  };
  status = npu_wifi_mt7996_tdma_delivery_initialize(&platform->delivery,
                                                    &delivery_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  publish_hardware(platform);
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

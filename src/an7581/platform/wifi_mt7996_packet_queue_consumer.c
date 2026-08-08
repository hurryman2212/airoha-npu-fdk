/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_packet_queue_consumer.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/wifi_mt7996_packet_queue.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/region.h"

static enum npu_runtime_result platform_forward_packet(
    void *context, const struct npu_wifi_mt7996_packet_delivery *delivery) {
  struct an7581_wifi_mt7996_packet_queue_consumer_platform *platform = context;

  if (platform == NULL || platform->forward == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return platform->forward(platform->forward_context, delivery);
}

static enum npu_runtime_result platform_release_packet(void *context,
                                                       uint16_t packet_id) {
  struct an7581_wifi_mt7996_packet_queue_consumer_platform *platform = context;

  if (platform == NULL || platform->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_packet_id_pool_release(platform->packet_pool, packet_id);
}

enum npu_runtime_result
an7581_wifi_mt7996_packet_queue_consumer_memory_resolve_band(
    const struct npu_wifi_configuration *configuration, uint32_t band,
    struct an7581_wifi_mt7996_packet_queue_consumer_memory *memory) {
  static const uint32_t region_types[] = {
      NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_BAND0,
      NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_SECONDARY,
  };
  static const uint32_t region_addresses[] = {
      AN7581_WIFI_MT7996_PACKET_QUEUE_REGION_ADDRESS,
      AN7581_WIFI_MT7996_PACKET_QUEUE_SECONDARY_REGION_ADDRESS,
  };
  struct an7581_wifi_mt7996_packet_queue_consumer_memory candidate;
  struct npu_wifi_region queue_region;
  uint32_t packet_cached_address;
  uint32_t packet_span = NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_ID_LIMIT *
                         NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE;

  if (configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (band >= AN7581_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT ||
      !configuration->packet_buffer_address_valid ||
      !npu_wifi_mt7996_fixed_region_lookup(region_types[band], &queue_region) ||
      queue_region.address != region_addresses[band] ||
      !an7581_dma_buffer_map(configuration->packet_buffer_address, packet_span,
                             NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_SIZE,
                             &packet_cached_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  candidate.entries =
      (volatile struct npu_wifi_mt7996_packet_queue_entry *)(uintptr_t)
          queue_region.address;
  candidate.packet_cached_memory =
      (volatile uint8_t *)(uintptr_t)packet_cached_address;
  candidate.entry_memory_size = AN7581_WIFI_MT7996_PACKET_QUEUE_MEMORY_SIZE;
  candidate.packet_memory_size = packet_span;
  candidate.packet_physical_base = configuration->packet_buffer_address;
  candidate.packet_id_limit = NPU_WIFI_MT7996_PACKET_QUEUE_PACKET_ID_LIMIT;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_wifi_mt7996_packet_queue_consumer_platform_initialize(
    struct an7581_wifi_mt7996_packet_queue_consumer_platform *platform,
    const struct an7581_wifi_mt7996_packet_queue_consumer_config *config) {
  struct npu_wifi_mt7996_packet_queue_consumer_config service_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->packet_pool == NULL || !config->packet_pool->initialized ||
      config->forward == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->packet_pool = config->packet_pool;
  platform->forward = config->forward;
  platform->forward_context = config->forward_context;
  service_config = (struct npu_wifi_mt7996_packet_queue_consumer_config){
      .entries = config->memory.entries,
      .packet_cached_memory = config->memory.packet_cached_memory,
      .operations =
          {
              .forward_packet = platform_forward_packet,
              .release_packet = platform_release_packet,
          },
      .diagnostic_counters = config->diagnostic_counters,
      .operation_context = platform,
      .entry_memory_size = config->memory.entry_memory_size,
      .packet_memory_size = config->memory.packet_memory_size,
      .packet_physical_base = config->memory.packet_physical_base,
      .packet_id_limit = config->memory.packet_id_limit,
      .consumer = config->consumer,
  };
  status = npu_wifi_mt7996_packet_queue_consumer_initialize(&platform->service,
                                                            &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

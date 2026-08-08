/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_packet_queue.h"

#include "an7581/runtime/memory.h"

static enum npu_runtime_result platform_acquire(void *context) {
  struct an7581_wifi_mt7996_packet_queue_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return an7581_hardware_mutex_acquire(&platform->mutexes, 0U);
}

static enum npu_runtime_result platform_release(void *context) {
  struct an7581_wifi_mt7996_packet_queue_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return an7581_hardware_mutex_release(&platform->mutexes, 0U);
}

enum npu_runtime_result an7581_wifi_mt7996_packet_queue_memory_resolve(
    struct an7581_wifi_mt7996_packet_queue_memory *memory) {
  return an7581_wifi_mt7996_packet_queue_memory_resolve_band(0U, memory);
}

enum npu_runtime_result an7581_wifi_mt7996_packet_queue_memory_resolve_band(
    uint32_t band, struct an7581_wifi_mt7996_packet_queue_memory *memory) {
  static const uint32_t complete_region_types[] = {
      NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_BAND0,
      NPU_WIFI_MT7996_FIXED_PACKET_QUEUE_SECONDARY,
  };
  static const uint32_t complete_region_addresses[] = {
      AN7581_WIFI_MT7996_PACKET_QUEUE_REGION_ADDRESS,
      AN7581_WIFI_MT7996_PACKET_QUEUE_SECONDARY_REGION_ADDRESS,
  };
  static const uint32_t fragment_region_types[] = {
      NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_BAND0,
      NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_SECONDARY,
  };
  static const uint32_t fragment_region_addresses[] = {
      AN7581_WIFI_MT7996_FRAGMENT_QUEUE_REGION_ADDRESS,
      AN7581_WIFI_MT7996_FRAGMENT_QUEUE_SECONDARY_REGION_ADDRESS,
  };
  struct an7581_wifi_mt7996_packet_queue_memory candidate;
  struct npu_wifi_region region;

  if (memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (band >= AN7581_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!npu_wifi_mt7996_fixed_region_lookup(complete_region_types[band],
                                           &region) ||
      region.address != complete_region_addresses[band])
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  candidate.entries =
      (volatile struct npu_wifi_mt7996_packet_queue_entry *)(uintptr_t)
          region.address;
  candidate.entry_memory_size = AN7581_WIFI_MT7996_PACKET_QUEUE_MEMORY_SIZE;
  if (!npu_wifi_mt7996_fixed_region_lookup(fragment_region_types[band],
                                           &region) ||
      region.address != fragment_region_addresses[band])
    return NPU_RUNTIME_OUT_OF_RANGE;
  candidate.fragment_entries =
      (volatile struct npu_wifi_mt7996_fragment_queue_entry *)(uintptr_t)
          region.address;
  candidate.fragment_entry_memory_size =
      AN7581_WIFI_MT7996_FRAGMENT_QUEUE_MEMORY_SIZE;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_packet_queue_platform_initialize(
    struct an7581_wifi_mt7996_packet_queue_platform *platform,
    const struct an7581_wifi_mt7996_packet_queue_config *config) {
  static const uint32_t mutex_handles[] = {
      AN7581_WIFI_MT7996_PACKET_QUEUE_MUTEX_HANDLE};
  struct npu_wifi_mt7996_packet_queue_config service_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->memory.entries == NULL ||
      config->memory.entry_memory_size <
          AN7581_WIFI_MT7996_PACKET_QUEUE_MEMORY_SIZE ||
      (uint32_t)config->band >= AN7581_WIFI_MT7996_PACKET_QUEUE_BAND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (config->memory.fragment_entries != NULL &&
      config->memory.fragment_entry_memory_size <
          AN7581_WIFI_MT7996_FRAGMENT_QUEUE_MEMORY_SIZE)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  status = an7581_hardware_mutex_bank_initialize(
      &platform->mutexes, config->hart_id, mutex_handles,
      sizeof(mutex_handles) / sizeof(mutex_handles[0]));
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  service_config = (struct npu_wifi_mt7996_packet_queue_config){
      .entries = config->memory.entries,
      .fragment_entries = config->memory.fragment_entries,
      .acquire = platform_acquire,
      .release = platform_release,
      .lock_context = platform,
      .diagnostic_counters = config->diagnostic_counters,
      .entry_memory_size = config->memory.entry_memory_size,
      .fragment_entry_memory_size = config->memory.fragment_entry_memory_size,
      .producer = config->producer,
      .fragment_producer = config->fragment_producer,
      .band = config->band,
  };
  status = npu_wifi_mt7996_packet_queue_initialize(&platform->service,
                                                   &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

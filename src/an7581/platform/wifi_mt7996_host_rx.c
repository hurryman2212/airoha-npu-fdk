/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_host_rx.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/vdma.h"
#include "an7581/runtime/memory.h"

static enum npu_runtime_result platform_copy(void *context,
                                             uint32_t source_address,
                                             uint32_t destination_address,
                                             uint32_t length) {
  struct an7581_wifi_mt7996_host_rx_platform *platform = context;

  if (platform == NULL || platform->vdma_poll_limit == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return an7581_vdma_copy(AN7581_WIFI_MT7996_HOST_RX_VDMA_CHANNEL,
                          source_address, destination_address, length,
                          platform->vdma_poll_limit);
}

enum npu_runtime_result an7581_wifi_mt7996_host_rx_memory_resolve(
    uint32_t band, struct an7581_wifi_mt7996_host_rx_memory *memory) {
  static const uint32_t ring_base_controls[] = {
      AN7581_WIFI_MT7996_HOST_RX_RING0_BASE_CONTROL,
      AN7581_WIFI_MT7996_HOST_RX_RING1_BASE_CONTROL,
  };
  static const uint32_t producer_addresses[] = {
      AN7581_WIFI_MT7996_HOST_RX_RING0_PRODUCER,
      AN7581_WIFI_MT7996_HOST_RX_RING1_PRODUCER,
  };
  struct an7581_wifi_mt7996_host_rx_memory candidate;
  uint32_t descriptor_address;
  uint32_t descriptor_span =
      NPU_WIFI_MT7996_HOST_RX_ENTRY_COUNT * NPU_WIFI_MT7996_HOST_RX_ENTRY_SIZE;
  uint32_t ring_physical_base;

  if (memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (band >= AN7581_WIFI_MT7996_HOST_RX_BAND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ring_physical_base = an7581_mmio_read32(ring_base_controls[band]);
  if (ring_physical_base == 0U ||
      !an7581_dma_buffer_map(ring_physical_base, descriptor_span,
                             sizeof(uint32_t), &descriptor_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  candidate.descriptors =
      (volatile struct npu_wifi_mt7996_host_rx_descriptor *)(uintptr_t)
          descriptor_address;
  candidate.producer = (volatile uint32_t *)(uintptr_t)producer_addresses[band];
  candidate.descriptor_memory_size = descriptor_span;
  candidate.ring_physical_base = ring_physical_base;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_host_rx_platform_initialize(
    struct an7581_wifi_mt7996_host_rx_platform *platform,
    const struct an7581_wifi_mt7996_host_rx_config *config) {
  struct npu_wifi_mt7996_host_rx_config service_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->vdma_poll_limit == 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->vdma_poll_limit = config->vdma_poll_limit;
  service_config = (struct npu_wifi_mt7996_host_rx_config){
      .descriptors = config->memory.descriptors,
      .producer = config->memory.producer,
      .copy = platform_copy,
      .copy_context = platform,
      .diagnostic_counters = config->diagnostic_counters,
      .descriptor_memory_size = config->memory.descriptor_memory_size,
  };
  status =
      npu_wifi_mt7996_host_rx_initialize(&platform->service, &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

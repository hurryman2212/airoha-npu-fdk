/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_tx_packet_slow_path.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/vdma.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/packet_id_pool.h"

struct slow_path_band_profile {
  uint32_t configuration_interface;
  uint32_t output_get_interface;
  uint32_t output_count;
};

static bool profile_lookup(uint32_t band_index,
                           struct slow_path_band_profile *profile) {
  if (profile == NULL || band_index >= NPU_WIFI_MT7996_TX_BAND_COUNT)
    return false;

  *profile = (struct slow_path_band_profile){
      .configuration_interface =
          band_index == 0U ? AN7581_WIFI_MT7996_TX_SLOW_PATH_BAND0_INTERFACE
                           : AN7581_WIFI_MT7996_TX_SLOW_PATH_BAND2_INTERFACE,
      .output_get_interface = band_index == 0U ? 5U : 7U,
      .output_count = band_index == 0U
                          ? NPU_WIFI_MT7996_TX_BAND0_DESCRIPTOR_COUNT
                          : NPU_WIFI_MT7996_TX_SECONDARY_DESCRIPTOR_COUNT,
  };
  return true;
}

static bool resolve_band(uint32_t dynamic_base,
                         const struct npu_wifi_configuration *configuration,
                         uint32_t band_index,
                         struct npu_wifi_tx_slow_path_band_config *band) {
  const struct npu_wifi_interface_configuration *interface_configuration;
  struct slow_path_band_profile profile;
  struct npu_wifi_region input_region;
  struct npu_wifi_region output_region;
  uint32_t local_staging_address;
  uint32_t staging_size;

  if (!profile_lookup(band_index, &profile))
    return false;

  staging_size = profile.output_count * NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE;
  interface_configuration =
      &configuration->interface[profile.configuration_interface];
  if ((interface_configuration->valid_fields &
       (NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS |
        NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE)) !=
          (NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS |
           NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE) ||
      interface_configuration->tx_ring_pcie_address == 0U ||
      (interface_configuration->tx_ring_pcie_address &
       (sizeof(uint32_t) - 1U)) != 0U ||
      interface_configuration->tx_ring_pcie_address >
          UINT32_MAX - sizeof(struct npu_wifi_tx_ring_registers) ||
      !an7581_dma_buffer_map(interface_configuration->tx_buffer_space_base,
                             staging_size, NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE,
                             &local_staging_address) ||
      !npu_wifi_tx_packet_space_region_lookup(band_index, &input_region) ||
      input_region.usable_size != NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT *
                                      NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE ||
      !npu_wifi_tx_ring_region_lookup(
          dynamic_base, profile.output_get_interface, &output_region) ||
      output_region.usable_size !=
          profile.output_count * NPU_WIFI_TX_DESCRIPTOR_SIZE)
    return false;

  band->input_descriptors =
      (volatile struct npu_wifi_tx_packet_descriptor *)(uintptr_t)
          input_region.address;
  band->output_descriptors =
      (volatile struct npu_wifi_tx_descriptor *)(uintptr_t)
          output_region.address;
  band->registers = (volatile struct npu_wifi_tx_ring_registers *)(uintptr_t)
                        interface_configuration->tx_ring_pcie_address;
  band->staging_memory = (volatile uint8_t *)(uintptr_t)local_staging_address;
  band->staging_memory_size = staging_size;
  band->staging_physical_base = interface_configuration->tx_buffer_space_base;
  band->output_descriptor_count = profile.output_count;
  return true;
}

enum npu_runtime_result an7581_wifi_tx_slow_path_memory_resolve(
    uint32_t dynamic_base, const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_tx_slow_path_memory *memory) {
  struct an7581_wifi_tx_slow_path_memory candidate;

  if (configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!configuration->token_id_size_valid ||
      configuration->token_id_size == 0U ||
      configuration->token_id_size > NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  if (!resolve_band(dynamic_base, configuration,
                    AN7581_WIFI_TX_SLOW_PATH_BAND0_ACTIVATION_INDEX,
                    &candidate.band[0]) ||
      !resolve_band(dynamic_base, configuration,
                    AN7581_WIFI_TX_SLOW_PATH_SECONDARY_ACTIVATION_INDEX,
                    &candidate.band[1]))
    return NPU_RUNTIME_OUT_OF_RANGE;

  candidate.token_id_limit = configuration->token_id_size;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result platform_vdma_copy(void *context,
                                                  uint32_t source_address,
                                                  uint32_t destination_address,
                                                  uint32_t length) {
  struct an7581_wifi_tx_slow_path_platform *platform = context;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return an7581_vdma_copy(AN7581_WIFI_TX_SLOW_PATH_VDMA_CHANNEL, source_address,
                          destination_address, length,
                          platform->vdma_poll_limit);
}

static void platform_retry_delay(void *context, uint32_t iterations) {
  (void)context;
  while (iterations != 0U) {
    an7581_cpu_relax();
    --iterations;
  }
}

enum npu_runtime_result an7581_wifi_tx_slow_path_platform_initialize(
    struct an7581_wifi_tx_slow_path_platform *platform,
    const struct an7581_wifi_tx_slow_path_config *config) {
  struct npu_wifi_tx_slow_path_config service_config;
  uint32_t band_index;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->vdma_poll_limit == 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  (void)npu_memset(&service_config, 0U, sizeof(service_config));
  for (band_index = 0U; band_index < NPU_WIFI_MT7996_TX_BAND_COUNT;
       ++band_index)
    service_config.band[band_index] = config->memory.band[band_index];
  platform->vdma_poll_limit = config->vdma_poll_limit;
  service_config.copy = platform_vdma_copy;
  service_config.delay = platform_retry_delay;
  service_config.copy_context = platform;
  service_config.delay_context = platform;
  service_config.producer_state = config->producer_state;
  service_config.stop_requested = config->stop_requested;
  service_config.token_id_limit = config->memory.token_id_limit;
  status =
      npu_wifi_tx_slow_path_initialize(&platform->service, &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_tdm_tx_forward.h"

#include "an7581/platform/mmio.h"
#include "an7581/runtime/memory.h"

static void platform_retry_delay(void *context, uint32_t iterations) {
  (void)context;
  while (iterations != 0U) {
    an7581_cpu_relax();
    --iterations;
  }
}

static bool interface_registers_resolve(
    const struct npu_wifi_configuration *configuration, uint32_t interface,
    volatile struct npu_wifi_tx_ring_registers **registers) {
  const struct npu_wifi_interface_configuration *interface_configuration;
  uint32_t address;

  interface_configuration = &configuration->interface[interface];
  address = interface_configuration->tx_ring_pcie_address;
  if ((interface_configuration->valid_fields &
       NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS) == 0U ||
      address == 0U || address > UINT32_MAX - sizeof(**registers) ||
      (address & (sizeof(uint32_t) - 1U)) != 0U)
    return false;

  *registers = (volatile struct npu_wifi_tx_ring_registers *)(uintptr_t)address;
  return true;
}

static bool band_regions_resolve(uint32_t dynamic_base,
                                 uint32_t descriptor_region_type,
                                 uint32_t record_region_type,
                                 uint32_t descriptor_count,
                                 struct npu_wifi_tdm_tx_band_config *band) {
  struct npu_wifi_region descriptor_region;
  struct npu_wifi_region record_region;
  uint32_t descriptor_size = descriptor_count * NPU_WIFI_TX_DESCRIPTOR_SIZE;
  uint32_t record_size =
      descriptor_count * NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE;

  if (!npu_wifi_mt7996_dynamic_region_lookup(
          dynamic_base, descriptor_region_type, &descriptor_region) ||
      !npu_wifi_mt7996_dynamic_region_lookup(dynamic_base, record_region_type,
                                             &record_region) ||
      descriptor_region.usable_size != descriptor_size ||
      record_region.usable_size != record_size)
    return false;

  band->descriptors = (volatile struct npu_wifi_tx_descriptor *)(uintptr_t)
                          descriptor_region.address;
  band->records = (volatile struct npu_wifi_tx_buffer_space_record *)(uintptr_t)
                      record_region.address;
  band->record_physical_base = record_region.address;
  band->descriptor_count = descriptor_count;
  return true;
}

enum npu_runtime_result an7581_wifi_tdm_tx_forward_config_resolve(
    uint32_t dynamic_base, const struct npu_wifi_configuration *configuration,
    struct npu_wifi_tdm_tx_forward_config *config) {
  struct npu_wifi_tdm_tx_forward_config candidate;

  if (configuration == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  if (!band_regions_resolve(
          dynamic_base, NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_BAND0,
          NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_BAND0,
          NPU_WIFI_TDM_TX_BAND0_DESCRIPTOR_COUNT, &candidate.band[0]) ||
      !band_regions_resolve(
          dynamic_base, NPU_WIFI_MT7996_DYNAMIC_TX_DESCRIPTORS_SECONDARY,
          NPU_WIFI_MT7996_DYNAMIC_TX_BUFFER_SPACE_SECONDARY,
          NPU_WIFI_TDM_TX_SECONDARY_DESCRIPTOR_COUNT, &candidate.band[1]) ||
      !interface_registers_resolve(configuration,
                                   AN7581_WIFI_TDM_TX_BAND0_INTERFACE,
                                   &candidate.band[0].registers) ||
      !interface_registers_resolve(configuration,
                                   AN7581_WIFI_TDM_TX_BAND2_INTERFACE,
                                   &candidate.band[1].registers))
    return NPU_RUNTIME_OUT_OF_RANGE;

  *config = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_tdm_tx_forward_platform_initialize(
    struct npu_wifi_tdm_tx_forward *forwarder,
    const struct an7581_wifi_tdm_tx_forward_platform_config *config) {
  struct npu_wifi_tdm_tx_forward_config service_config;
  enum npu_runtime_result status;

  if (forwarder == NULL || config == NULL || config->configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  status = an7581_wifi_tdm_tx_forward_config_resolve(
      config->dynamic_base, config->configuration, &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  if (config->band0_diagnostic_counters != NULL) {
    service_config.band[0].diagnostic_counters.current_descriptor_waits =
        &config->band0_diagnostic_counters->tdm_tx_current_descriptor_waits;
    service_config.band[0].diagnostic_counters.descriptor_publish_retries =
        &config->band0_diagnostic_counters->tdm_tx_descriptor_publish_retries;
    service_config.band[0].diagnostic_counters.lookahead_descriptor_waits =
        &config->band0_diagnostic_counters->tdm_tx_lookahead_descriptor_waits;
  }
  if (config->band1_diagnostic_counters != NULL) {
    service_config.band[1].diagnostic_counters.current_descriptor_waits =
        &config->band1_diagnostic_counters->tdm_tx_current_descriptor_waits;
    service_config.band[1].diagnostic_counters.descriptor_publish_retries =
        &config->band1_diagnostic_counters->tdm_tx_descriptor_publish_retries;
    service_config.band[1].diagnostic_counters.lookahead_descriptor_waits =
        &config->band1_diagnostic_counters->tdm_tx_lookahead_descriptor_waits;
  }
  service_config.producer_state = config->producer_state;
  service_config.delay = platform_retry_delay;
  service_config.delay_context = forwarder;
  service_config.stop_requested = &config->configuration->inode_stop_requested;
  return npu_wifi_tdm_tx_forward_initialize(forwarder, &service_config);
}

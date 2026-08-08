/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_TX_FAST_PATH_H
#define AN7581_WIFI_TX_FAST_PATH_H

#include "an7581/platform/wifi_tdm_rx.h"
#include "an7581/platform/wifi_tdm_tx_forward.h"
#include "an7581/platform/wifi_tx_packet_slow_path.h"
#include "an7581/services/wifi/tx_fast_path_runtime.h"

struct an7581_wifi_tx_fast_path_memory {
  struct an7581_wifi_tdm_rx_memory tdm_rx;
  struct an7581_wifi_tx_slow_path_memory slow_path;
};

struct an7581_wifi_tx_fast_path_config {
  struct npu_wifi_sram_allocator *allocator;
  const struct an7581_wifi_tx_fast_path_memory *memory_override;
  struct npu_wifi_packet_id_pool *token_pool;
  const struct npu_wifi_configuration *wifi_configuration;
  const volatile uint32_t *initialization_complete;
  const volatile uint8_t *packet_space_ready;
  const volatile uint8_t *configuration_state;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  uint32_t dynamic_base;
  uint32_t token_state_count;
  uint32_t vdma_poll_limit;
};

struct an7581_wifi_tx_fast_path_platform {
  struct npu_wifi_tx_producer_state producer_state;
  struct npu_wifi_tdm_tx_forward forwarder;
  struct an7581_wifi_tx_slow_path_platform slow_path;
  struct an7581_wifi_tdm_rx_platform tdm_rx;
  struct npu_wifi_tx_fast_path_runtime runtime;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_tx_fast_path_memory_resolve(
    struct npu_wifi_sram_allocator *allocator, uint32_t dynamic_base,
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_tx_fast_path_memory *memory);
enum npu_runtime_result an7581_wifi_tx_fast_path_platform_initialize(
    struct an7581_wifi_tx_fast_path_platform *platform,
    const struct an7581_wifi_tx_fast_path_config *config);

#endif

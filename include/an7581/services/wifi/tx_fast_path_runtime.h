/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_FAST_PATH_RUNTIME_H
#define NPU_WIFI_TX_FAST_PATH_RUNTIME_H

#include "an7581/services/wifi/tdm_tx_forward.h"
#include "an7581/services/wifi/tx_packet_slow_path.h"

#define NPU_WIFI_TX_FAST_PATH_CONFIGURATION_READY UINT32_C(3)
#define NPU_WIFI_TX_FAST_PATH_MINIMUM_FREE UINT32_C(6)
#define NPU_WIFI_TX_FAST_PATH_TDM_RESERVE UINT32_C(7)
#define NPU_WIFI_TX_FAST_PATH_IMMEDIATE_CONTINUE_FREE UINT32_C(131)

struct npu_wifi_tx_fast_path_runtime_config {
  struct npu_wifi_tx_slow_path *slow_path;
  struct npu_wifi_tdm_rx *tdm_receiver;
  struct npu_wifi_tdm_tx_forward *tdm_forwarder;
  const volatile uint32_t *initialization_complete;
  const volatile uint8_t *packet_space_ready;
  const volatile uint8_t *configuration_state;
};

struct npu_wifi_tx_fast_path_band_result {
  struct npu_wifi_tx_slow_path_result slow_path;
  enum npu_runtime_result slow_path_status;
  enum npu_runtime_result tdm_status;
  uint32_t free_descriptor_count;
  uint32_t tdm_budget;
  uint32_t tdm_processed_count;
  bool slow_path_attempted;
  bool tdm_attempted;
  bool output_capacity_limited;
  bool dma_refresh_recommended;
};

struct npu_wifi_tx_fast_path_step_result {
  struct npu_wifi_tx_fast_path_band_result band[NPU_WIFI_MT7996_TX_BAND_COUNT];
  uint32_t tdm_processed_count;
  bool waiting_for_initialization;
  bool waiting_for_packet_space;
  bool waiting_for_configuration;
  bool indices_reset;
  bool idle;
  bool should_backoff;
};

struct npu_wifi_tx_fast_path_runtime {
  struct npu_wifi_tx_slow_path *slow_path;
  struct npu_wifi_tdm_rx *tdm_receiver;
  struct npu_wifi_tdm_tx_forward *tdm_forwarder;
  const volatile uint32_t *initialization_complete;
  const volatile uint8_t *packet_space_ready;
  const volatile uint8_t *configuration_state;
  uint16_t dma_index[NPU_WIFI_MT7996_TX_BAND_COUNT];
  uint32_t step_count;
  uint32_t ready_step_count;
  uint32_t configuration_reset_count;
  uint32_t output_capacity_limit_count;
  bool initialized;
};

enum npu_runtime_result npu_wifi_tx_fast_path_runtime_initialize(
    struct npu_wifi_tx_fast_path_runtime *runtime,
    const struct npu_wifi_tx_fast_path_runtime_config *config);
enum npu_runtime_result npu_wifi_tx_fast_path_runtime_step(
    struct npu_wifi_tx_fast_path_runtime *runtime,
    struct npu_wifi_tx_fast_path_step_result *result);

#endif

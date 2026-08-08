/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_tx_fast_path_board_binding.h"

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_board_binding_resolve(
    const struct an7581_wifi_mt7996_tx_fast_path_board_binding *binding,
    struct an7581_core2_dispatch *dispatch,
    struct npu_wifi_sram_allocator *shared_allocator,
    struct npu_wifi_packet_id_pool *shared_packet_pool,
    struct an7581_wifi_mt7996_runtime_readiness_state *readiness,
    volatile struct npu_wifi_mt7996_band0_diagnostic_counters
        *band0_diagnostic_counters,
    volatile struct npu_wifi_mt7996_band1_diagnostic_counters
        *band1_diagnostic_counters,
    struct an7581_wifi_mt7996_tx_fast_path_platform *platform,
    struct an7581_wifi_mt7996_tx_fast_path_board_configuration *configuration) {
  struct an7581_wifi_mt7996_tx_fast_path_platform_config platform_config;
  enum npu_runtime_result status;

  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *configuration =
      (struct an7581_wifi_mt7996_tx_fast_path_board_configuration){0};
  if (binding == NULL || !binding->activation_allowed)
    return NPU_RUNTIME_SUCCESS;
  if (binding->wake_worker == NULL || binding->prepare_stop == NULL ||
      binding->resume == NULL || binding->vdma_poll_limit == 0U ||
      dispatch == NULL || shared_allocator == NULL ||
      shared_packet_pool == NULL || readiness == NULL || platform == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  platform_config = (struct an7581_wifi_mt7996_tx_fast_path_platform_config){
      .dispatch = dispatch,
      .shared_allocator = shared_allocator,
      .shared_packet_pool = shared_packet_pool,
      .readiness = readiness,
      .band0_diagnostic_counters = band0_diagnostic_counters,
      .band1_diagnostic_counters = band1_diagnostic_counters,
      .wake_worker = binding->wake_worker,
      .wake_context = binding->wake_context,
      .vdma_poll_limit = binding->vdma_poll_limit,
  };
  status = an7581_wifi_mt7996_tx_fast_path_platform_initialize(
      platform, &platform_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  configuration->platform = platform;
  configuration->prepare_stop = binding->prepare_stop;
  configuration->resume = binding->resume;
  configuration->stop_context = binding->stop_context;
  configuration->activation_allowed = true;
  return NPU_RUNTIME_SUCCESS;
}

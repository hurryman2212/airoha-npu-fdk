/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_completion_runtime.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

enum npu_runtime_result an7581_wifi_mt7996_runtime_readiness_initialize(
    struct an7581_wifi_mt7996_runtime_readiness_state *state) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(state, 0U, sizeof(*state));
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_offload(
    struct an7581_wifi_mt7996_runtime_readiness_state *state,
    bool initialized) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  state->offload_initialized = initialized ? 1U : 0U;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_tx_state(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool enabled,
    bool configured) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  state->tx_done_enabled = enabled ? 1U : 0U;
  state->tx_configuration_state =
      configured ? NPU_WIFI_MT7996_COMPLETION_READY_STATE : 0U;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_rx_state(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool ring_enabled,
    bool configured) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  if (!ring_enabled) {
    state->rx_ring_enabled = 0U;
    an7581_dma_memory_barrier();
  }
  state->rx_configuration_state =
      configured ? NPU_WIFI_MT7996_COMPLETION_READY_STATE : 0U;
  an7581_dma_memory_barrier();
  if (ring_enabled) {
    state->rx_ring_enabled = 1U;
    an7581_dma_memory_barrier();
  }
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_host_rx_state(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool ready) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  state->host_rx_rings_ready =
      ready ? NPU_WIFI_MT7996_COMPLETION_HOST_RX_READY : 0U;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_readiness_resolve(
    struct an7581_wifi_mt7996_runtime_readiness_state *state,
    struct npu_wifi_mt7996_completion_readiness *readiness) {
  if (state == NULL || readiness == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *readiness = (struct npu_wifi_mt7996_completion_readiness){
      .offload_initialized = &state->offload_initialized,
      .tx_done_enabled = &state->tx_done_enabled,
      .tx_configuration_state = &state->tx_configuration_state,
      .tx_done_activity = &state->tx_done_activity,
      .rx_ring_enabled = &state->rx_ring_enabled,
      .rx_configuration_state = &state->rx_configuration_state,
      .host_rx_rings_ready = &state->host_rx_rings_ready,
  };
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_runtime_initialize(
    struct an7581_wifi_mt7996_completion_runtime *platform,
    const struct an7581_wifi_mt7996_completion_runtime_config *config) {
  struct npu_wifi_mt7996_completion_runtime_config service_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->tx_done == NULL || !config->tx_done->initialized ||
      config->packet_consumers[0] == NULL ||
      !config->packet_consumers[0]->initialized ||
      config->packet_consumers[1] == NULL ||
      !config->packet_consumers[1]->initialized ||
      config->fragment_consumer == NULL ||
      !config->fragment_consumer->initialized ||
      config->host_tx_consumer == NULL ||
      !config->host_tx_consumer->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  service_config = (struct npu_wifi_mt7996_completion_runtime_config){
      .tx_done = &config->tx_done->service,
      .packet_consumers =
          {
              &config->packet_consumers[0]->service,
              &config->packet_consumers[1]->service,
          },
      .fragment_consumer = config->fragment_consumer,
      .host_tx_consumer = config->host_tx_consumer,
      .readiness = config->readiness,
      .tx_done_budget = config->tx_done_budget,
      .packet_queue_budgets =
          {
              config->band0_budget,
              NPU_WIFI_MT7996_COMPLETION_SECONDARY_BUDGET,
          },
  };
  status = npu_wifi_mt7996_completion_runtime_initialize(&platform->service,
                                                         &service_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

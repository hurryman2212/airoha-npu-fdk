/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_fast_path_runtime.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool pointer_is_word_aligned(const volatile void *pointer) {
  return pointer != NULL &&
         ((uintptr_t)pointer & (sizeof(uint32_t) - 1U)) == 0U;
}

static bool band_bindings_match(const struct npu_wifi_tx_slow_path *slow_path,
                                const struct npu_wifi_tdm_tx_forward *forwarder,
                                uint32_t band_index) {
  return slow_path->band[band_index].output_descriptors ==
             forwarder->band[band_index].descriptors &&
         slow_path->band[band_index].registers ==
             forwarder->band[band_index].registers &&
         slow_path->band[band_index].output_descriptor_count ==
             forwarder->band[band_index].descriptor_count;
}

static bool
pipeline_is_valid(const struct npu_wifi_tx_fast_path_runtime_config *config) {
  uint32_t band_index;

  if (config == NULL || config->slow_path == NULL ||
      config->tdm_receiver == NULL || config->tdm_forwarder == NULL ||
      !config->slow_path->initialized || !config->tdm_receiver->initialized ||
      !config->tdm_forwarder->initialized ||
      config->slow_path->producer_state !=
          config->tdm_forwarder->producer_state ||
      !npu_wifi_tx_producer_state_is_valid(config->slow_path->producer_state) ||
      config->tdm_receiver->dispatch != npu_wifi_tdm_tx_forward_dispatch ||
      config->tdm_receiver->publish_dispatch !=
          npu_wifi_tdm_tx_forward_publish ||
      config->tdm_receiver->dispatch_context != config->tdm_forwarder ||
      !pointer_is_word_aligned(config->initialization_complete) ||
      config->packet_space_ready == NULL || config->configuration_state == NULL)
    return false;

  for (band_index = 0U; band_index < NPU_WIFI_MT7996_TX_BAND_COUNT;
       ++band_index) {
    if (!band_bindings_match(config->slow_path, config->tdm_forwarder,
                             band_index))
      return false;
  }
  return true;
}

enum npu_runtime_result npu_wifi_tx_fast_path_runtime_initialize(
    struct npu_wifi_tx_fast_path_runtime *runtime,
    const struct npu_wifi_tx_fast_path_runtime_config *config) {
  if (runtime == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (runtime->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!pipeline_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(runtime, 0U, sizeof(*runtime));
  runtime->slow_path = config->slow_path;
  runtime->tdm_receiver = config->tdm_receiver;
  runtime->tdm_forwarder = config->tdm_forwarder;
  runtime->initialization_complete = config->initialization_complete;
  runtime->packet_space_ready = config->packet_space_ready;
  runtime->configuration_state = config->configuration_state;
  runtime->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static bool
runtime_state_is_valid(const struct npu_wifi_tx_fast_path_runtime *runtime) {
  struct npu_wifi_tx_fast_path_runtime_config config;

  if (runtime == NULL || !runtime->initialized)
    return false;
  config.slow_path = runtime->slow_path;
  config.tdm_receiver = runtime->tdm_receiver;
  config.tdm_forwarder = runtime->tdm_forwarder;
  config.initialization_complete = runtime->initialization_complete;
  config.packet_space_ready = runtime->packet_space_ready;
  config.configuration_state = runtime->configuration_state;
  return pipeline_is_valid(&config);
}

static bool runtime_is_ready(struct npu_wifi_tx_fast_path_runtime *runtime,
                             struct npu_wifi_tx_fast_path_step_result *result) {
  uint32_t initialization_complete;
  uint8_t configuration_state;
  uint8_t packet_space_ready;

  an7581_dma_memory_barrier();
  initialization_complete = *runtime->initialization_complete;
  packet_space_ready = *runtime->packet_space_ready;
  configuration_state = *runtime->configuration_state;
  an7581_dma_memory_barrier();

  result->waiting_for_initialization = initialization_complete == 0U;
  result->waiting_for_packet_space = packet_space_ready == 0U;
  result->waiting_for_configuration = (uint32_t)configuration_state !=
                                      NPU_WIFI_TX_FAST_PATH_CONFIGURATION_READY;
  if (result->waiting_for_configuration) {
    runtime->dma_index[0] = 0U;
    runtime->dma_index[1] = 0U;
    ++runtime->configuration_reset_count;
    result->indices_reset = true;
  }
  if (result->waiting_for_initialization || result->waiting_for_packet_space ||
      result->waiting_for_configuration) {
    result->idle = true;
    result->should_backoff = true;
    return false;
  }
  return true;
}

static uint32_t free_descriptor_count(uint16_t producer, uint16_t dma_index,
                                      uint32_t descriptor_count) {
  if (producer < dma_index)
    return (uint32_t)dma_index - (uint32_t)producer - 1U;
  return (uint32_t)dma_index + descriptor_count - (uint32_t)producer - 1U;
}

static bool status_is_hard_failure(enum npu_runtime_result status) {
  return status != NPU_RUNTIME_SUCCESS && status != NPU_RUNTIME_EMPTY &&
         status != NPU_RUNTIME_FULL && status != NPU_RUNTIME_REJECTED;
}

static enum npu_runtime_result
process_band(struct npu_wifi_tx_fast_path_runtime *runtime, uint32_t band_index,
             struct npu_wifi_tx_fast_path_band_result *result) {
  volatile struct npu_wifi_tx_ring_registers *registers =
      runtime->tdm_forwarder->band[band_index].registers;
  uint32_t descriptor_count =
      runtime->tdm_forwarder->band[band_index].descriptor_count;
  uint32_t dma_index;
  uint16_t producer;

  result->slow_path_status = NPU_RUNTIME_EMPTY;
  result->tdm_status = NPU_RUNTIME_EMPTY;
  an7581_dma_memory_barrier();
  dma_index = registers->dma_index;
  an7581_dma_memory_barrier();
  if (dma_index >= descriptor_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  runtime->dma_index[band_index] = (uint16_t)dma_index;
  producer = runtime->tdm_forwarder->producer_state->index[band_index];
  result->free_descriptor_count = free_descriptor_count(
      producer, runtime->dma_index[band_index], descriptor_count);
  result->dma_refresh_recommended =
      result->free_descriptor_count <
      NPU_WIFI_TX_FAST_PATH_IMMEDIATE_CONTINUE_FREE;
  if (result->free_descriptor_count < NPU_WIFI_TX_FAST_PATH_MINIMUM_FREE) {
    result->output_capacity_limited = true;
    ++runtime->output_capacity_limit_count;
    return NPU_RUNTIME_FULL;
  }

  result->slow_path_attempted = true;
  result->slow_path_status = npu_wifi_tx_slow_path_process(
      runtime->slow_path, band_index, &result->slow_path);
  if (status_is_hard_failure(result->slow_path_status) ||
      result->slow_path_status == NPU_RUNTIME_FULL)
    return result->slow_path_status;

  if (result->free_descriptor_count > NPU_WIFI_TX_FAST_PATH_TDM_RESERVE) {
    result->tdm_budget =
        result->free_descriptor_count - NPU_WIFI_TX_FAST_PATH_TDM_RESERVE;
    if (result->tdm_budget > NPU_WIFI_TDM_RX_BATCH_LIMIT)
      result->tdm_budget = NPU_WIFI_TDM_RX_BATCH_LIMIT;
    result->tdm_attempted = true;
    result->tdm_status = npu_wifi_tdm_rx_consume(runtime->tdm_receiver,
                                                 band_index, result->tdm_budget,
                                                 &result->tdm_processed_count);
    if (result->tdm_status != NPU_RUNTIME_SUCCESS &&
        result->tdm_status != NPU_RUNTIME_EMPTY)
      return result->tdm_status;
  }

  if (result->slow_path.consumed || result->tdm_processed_count != 0U)
    return NPU_RUNTIME_SUCCESS;
  if (result->slow_path_status == NPU_RUNTIME_REJECTED)
    return NPU_RUNTIME_SUCCESS;
  return NPU_RUNTIME_EMPTY;
}

enum npu_runtime_result npu_wifi_tx_fast_path_runtime_step(
    struct npu_wifi_tx_fast_path_runtime *runtime,
    struct npu_wifi_tx_fast_path_step_result *result) {
  static const uint8_t band_order[NPU_WIFI_MT7996_TX_BAND_COUNT] = {1U, 0U};
  enum npu_runtime_result first_failure = NPU_RUNTIME_SUCCESS;
  enum npu_runtime_result band_status;
  bool made_progress = false;
  bool capacity_limited = false;
  uint32_t order_index;
  uint32_t band_index;

  if (result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!runtime_state_is_valid(runtime))
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++runtime->step_count;
  if (!runtime_is_ready(runtime, result))
    return NPU_RUNTIME_EMPTY;
  ++runtime->ready_step_count;

  for (order_index = 0U; order_index < NPU_WIFI_MT7996_TX_BAND_COUNT;
       ++order_index) {
    band_index = band_order[order_index];
    band_status = process_band(runtime, band_index, &result->band[band_index]);
    result->tdm_processed_count += result->band[band_index].tdm_processed_count;
    if (result->band[band_index].slow_path.consumed ||
        result->band[band_index].tdm_processed_count != 0U)
      made_progress = true;
    if (band_status == NPU_RUNTIME_FULL)
      capacity_limited = true;
    else if (band_status != NPU_RUNTIME_SUCCESS &&
             band_status != NPU_RUNTIME_EMPTY &&
             first_failure == NPU_RUNTIME_SUCCESS)
      first_failure = band_status;
  }

  result->idle = !made_progress;
  result->should_backoff = first_failure != NPU_RUNTIME_SUCCESS ||
                           capacity_limited || result->idle ||
                           result->band[0].dma_refresh_recommended ||
                           result->band[1].dma_refresh_recommended;
  if (first_failure != NPU_RUNTIME_SUCCESS)
    return first_failure;
  if (made_progress)
    return NPU_RUNTIME_SUCCESS;
  if (capacity_limited)
    return NPU_RUNTIME_FULL;
  return NPU_RUNTIME_EMPTY;
}

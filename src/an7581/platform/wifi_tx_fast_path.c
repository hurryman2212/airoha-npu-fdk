/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_tx_fast_path.h"

#include "an7581/runtime/memory.h"

enum npu_runtime_result an7581_wifi_tx_fast_path_memory_resolve(
    struct npu_wifi_sram_allocator *allocator, uint32_t dynamic_base,
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_tx_fast_path_memory *memory) {
  struct an7581_wifi_tx_fast_path_memory candidate;
  enum npu_runtime_result status;

  if (allocator == NULL || configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  status = an7581_wifi_tx_slow_path_memory_resolve(dynamic_base, configuration,
                                                   &candidate.slow_path);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = an7581_wifi_tdm_rx_memory_resolve(allocator, configuration,
                                             &candidate.tdm_rx);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

static bool
configuration_is_valid(const struct an7581_wifi_tx_fast_path_config *config) {
  return config != NULL &&
         ((config->memory_override == NULL) != (config->allocator == NULL)) &&
         config->token_pool != NULL && config->token_pool->initialized &&
         config->wifi_configuration != NULL &&
         config->initialization_complete != NULL &&
         ((uintptr_t)config->initialization_complete &
          (sizeof(uint32_t) - 1U)) == 0U &&
         config->packet_space_ready != NULL &&
         config->configuration_state != NULL &&
         config->token_state_count >= config->token_pool->token_entry_count &&
         config->vdma_poll_limit != 0U;
}

enum npu_runtime_result an7581_wifi_tx_fast_path_platform_initialize(
    struct an7581_wifi_tx_fast_path_platform *platform,
    const struct an7581_wifi_tx_fast_path_config *config) {
  struct an7581_wifi_tx_fast_path_memory resolved_memory;
  const struct an7581_wifi_tx_fast_path_memory *memory;
  struct an7581_wifi_tdm_tx_forward_platform_config forwarder_config;
  struct npu_wifi_tx_fast_path_runtime_config runtime_config;
  struct an7581_wifi_tx_slow_path_config slow_path_config;
  struct an7581_wifi_tdm_rx_platform_config tdm_rx_config;
  enum npu_runtime_result status;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  if (config->memory_override != NULL) {
    memory = config->memory_override;
  } else {
    status = an7581_wifi_tx_fast_path_memory_resolve(
        config->allocator, config->dynamic_base, config->wifi_configuration,
        &resolved_memory);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    memory = &resolved_memory;
  }

  (void)npu_memset(platform, 0U, sizeof(*platform));
  forwarder_config = (struct an7581_wifi_tdm_tx_forward_platform_config){
      .configuration = config->wifi_configuration,
      .producer_state = &platform->producer_state,
      .band0_diagnostic_counters = config->band0_diagnostic_counters,
      .band1_diagnostic_counters = config->band1_diagnostic_counters,
      .dynamic_base = config->dynamic_base,
  };
  status = an7581_wifi_tdm_tx_forward_platform_initialize(&platform->forwarder,
                                                          &forwarder_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  (void)npu_memset(&slow_path_config, 0U, sizeof(slow_path_config));
  slow_path_config.memory = memory->slow_path;
  if (config->band0_diagnostic_counters != NULL) {
    slow_path_config.memory.band[0]
        .diagnostic_counters.waits_or_publish_failures =
        &config->band0_diagnostic_counters->tx_packet_waits_or_publish_failures;
    slow_path_config.memory.band[0]
        .diagnostic_counters.descriptor_publish_retries =
        &config->band0_diagnostic_counters
             ->tx_packet_descriptor_publish_retries;
    slow_path_config.memory.band[0]
        .diagnostic_counters.lookahead_descriptor_waits =
        &config->band0_diagnostic_counters
             ->tx_packet_lookahead_descriptor_waits;
  }
  if (config->band1_diagnostic_counters != NULL) {
    slow_path_config.memory.band[1]
        .diagnostic_counters.waits_or_publish_failures =
        &config->band1_diagnostic_counters->tx_packet_waits_or_publish_failures;
    slow_path_config.memory.band[1]
        .diagnostic_counters.descriptor_publish_retries =
        &config->band1_diagnostic_counters
             ->tx_packet_descriptor_publish_retries;
    slow_path_config.memory.band[1]
        .diagnostic_counters.lookahead_descriptor_waits =
        &config->band1_diagnostic_counters
             ->tx_packet_lookahead_descriptor_waits;
  }
  slow_path_config.producer_state = &platform->producer_state;
  slow_path_config.stop_requested =
      &config->wifi_configuration->inode_stop_requested;
  slow_path_config.vdma_poll_limit = config->vdma_poll_limit;
  status = an7581_wifi_tx_slow_path_platform_initialize(&platform->slow_path,
                                                        &slow_path_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  (void)npu_memset(&tdm_rx_config, 0U, sizeof(tdm_rx_config));
  tdm_rx_config.memory = memory->tdm_rx;
  tdm_rx_config.token_pool = config->token_pool;
  tdm_rx_config.dispatch = npu_wifi_tdm_tx_forward_dispatch;
  tdm_rx_config.publish_dispatch = npu_wifi_tdm_tx_forward_publish;
  tdm_rx_config.dispatch_context = &platform->forwarder;
  if (config->band0_diagnostic_counters != NULL) {
    tdm_rx_config.diagnostic_counters[0].descriptors_consumed =
        &config->band0_diagnostic_counters->tdm_rx_descriptors_consumed;
    tdm_rx_config.diagnostic_counters[0].token_allocation_failures =
        &config->band0_diagnostic_counters->tdm_rx_token_allocation_failures;
  }
  if (config->band1_diagnostic_counters != NULL) {
    tdm_rx_config.diagnostic_counters[1].descriptors_consumed =
        &config->band1_diagnostic_counters->tdm_rx_descriptors_consumed;
    tdm_rx_config.diagnostic_counters[1].token_allocation_failures =
        &config->band1_diagnostic_counters->tdm_rx_token_allocation_failures;
  }
  tdm_rx_config.token_state_count = config->token_state_count;
  status =
      an7581_wifi_tdm_rx_platform_initialize(&platform->tdm_rx, &tdm_rx_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  (void)npu_memset(&runtime_config, 0U, sizeof(runtime_config));
  runtime_config.slow_path = &platform->slow_path.service;
  runtime_config.tdm_receiver = &platform->tdm_rx.receiver;
  runtime_config.tdm_forwarder = &platform->forwarder;
  runtime_config.initialization_complete = config->initialization_complete;
  runtime_config.packet_space_ready = config->packet_space_ready;
  runtime_config.configuration_state = config->configuration_state;
  status = npu_wifi_tx_fast_path_runtime_initialize(&platform->runtime,
                                                    &runtime_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

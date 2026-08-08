/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_completion_pipeline.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/vdma.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/region.h"

#define AN7581_WIFI_MT7996_HOST_TX_FIRST_RING UINT32_C(2)
#define AN7581_WIFI_MT7996_HOST_TX_VDMA_CHANNEL UINT32_C(0)
#define AN7581_WIFI_MT7996_HOST_TX_VDMA_ALIAS UINT32_C(0x80000000)
#define AN7581_WIFI_MT7996_HOST_TX_TXWI_TOKEN_OFFSET UINT32_C(0x22)

static enum npu_runtime_result enqueue_completion(
    void *context,
    const struct npu_wifi_mt7996_tx_done_completion *completion) {
  struct an7581_wifi_mt7996_completion_pipeline *pipeline = context;

  if (pipeline == NULL || !pipeline->packet_queue.initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return npu_wifi_mt7996_packet_queue_enqueue_completion(
      &pipeline->packet_queue.service, completion);
}

static enum npu_runtime_result
forward_packet(void *context,
               const struct npu_wifi_mt7996_packet_delivery *delivery) {
  struct an7581_wifi_mt7996_completion_forward_context *forward = context;

  if (forward == NULL || forward->pipeline == NULL ||
      forward->band >= NPU_WIFI_MT7996_COMPLETION_BAND_COUNT ||
      !forward->pipeline->host_rx[forward->band].initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return npu_wifi_mt7996_host_rx_enqueue(
      &forward->pipeline->host_rx[forward->band].service, delivery);
}

static enum npu_runtime_result
forward_fragment(void *context,
                 const struct npu_wifi_mt7996_packet_delivery *delivery) {
  struct an7581_wifi_mt7996_completion_pipeline *pipeline = context;

  if (pipeline == NULL || !pipeline->host_rx[0].initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return npu_wifi_mt7996_host_rx_enqueue(&pipeline->host_rx[0].service,
                                         delivery);
}

static enum npu_runtime_result release_fragment_packet(void *context,
                                                       uint16_t packet_id) {
  struct an7581_wifi_mt7996_completion_pipeline *pipeline = context;

  if (pipeline == NULL || pipeline->packet_pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_wifi_packet_id_pool_release(pipeline->packet_pool, packet_id);
}

static void fragment_delay(void *context, uint32_t iterations) {
  (void)context;
  while (iterations != 0U) {
    an7581_cpu_relax();
    --iterations;
  }
}

static bool memory_is_consistent(
    const struct an7581_wifi_mt7996_completion_pipeline_memory *memory) {
  uint32_t band;

  if (memory->packet_queue.entries == NULL ||
      memory->packet_queue.entries != memory->packet_consumers[0].entries ||
      memory->tx_done.packet_cached_memory !=
          memory->packet_consumers[0].packet_cached_memory ||
      memory->tx_done.packet_cached_memory !=
          memory->packet_consumers[1].packet_cached_memory ||
      memory->host_tx_destinations[0] == NULL ||
      memory->host_tx_destinations[1] == NULL ||
      memory->secondary_fragment_entries == NULL ||
      memory->secondary_fragment_entry_memory_size <
          NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT *
              NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_SIZE)
    return false;

  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    if (memory->packet_consumers[band].entries == NULL ||
        memory->packet_consumers[band].packet_physical_base !=
            memory->tx_done.packet_physical_base ||
        memory->packet_consumers[band].packet_id_limit !=
            memory->tx_done.packet_id_limit ||
        memory->packet_consumers[band].packet_memory_size <
            memory->tx_done.packet_memory_size ||
        memory->host_rx[band].descriptors == NULL ||
        memory->host_rx[band].producer == NULL)
      return false;
  }
  return true;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_pipeline_memory_resolve(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_completion_pipeline_memory *memory) {
  struct an7581_wifi_mt7996_completion_pipeline_memory candidate;
  struct npu_wifi_region fragment_region;
  struct npu_wifi_region host_tx_destination_region;
  enum npu_runtime_result status;
  uint32_t band;

  if (configuration == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  status = an7581_wifi_mt7996_tx_done_memory_resolve(configuration,
                                                     &candidate.tx_done);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status =
      an7581_wifi_mt7996_packet_queue_memory_resolve(&candidate.packet_queue);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    status = an7581_wifi_mt7996_packet_queue_consumer_memory_resolve_band(
        configuration, band, &candidate.packet_consumers[band]);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    status = an7581_wifi_mt7996_host_rx_memory_resolve(
        band, &candidate.host_rx[band]);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    if (!npu_wifi_tx_packet_space_region_lookup(band,
                                                &host_tx_destination_region) ||
        host_tx_destination_region.usable_size !=
            NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT *
                NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE)
      return NPU_RUNTIME_OUT_OF_RANGE;
    candidate.host_tx_destinations[band] =
        (volatile struct npu_wifi_tx_packet_descriptor *)(uintptr_t)
            host_tx_destination_region.address;
  }
  if (!npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_FRAGMENT_QUEUE_SECONDARY, &fragment_region))
    return NPU_RUNTIME_OUT_OF_RANGE;
  candidate.secondary_fragment_entries =
      (volatile struct npu_wifi_mt7996_fragment_queue_entry *)(uintptr_t)
          fragment_region.address;
  candidate.secondary_fragment_entry_memory_size =
      AN7581_WIFI_MT7996_FRAGMENT_QUEUE_MEMORY_SIZE;
  if (!memory_is_consistent(&candidate))
    return NPU_RUNTIME_OUT_OF_RANGE;

  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result initialize_packet_queue(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  const struct an7581_wifi_mt7996_packet_queue_config queue_config = {
      .memory = config->memory.packet_queue,
      .diagnostic_counters =
          {
              .entries_enqueued = config->band0_diagnostic_counters != NULL
                                      ? &config->band0_diagnostic_counters
                                             ->packet_queue_entries_enqueued
                                      : NULL,
              .queue_full =
                  config->band0_diagnostic_counters != NULL
                      ? &config->band0_diagnostic_counters->packet_queue_full
                      : NULL,
          },
      .hart_id = AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART,
      .producer = config->packet_queue_producer,
  };

  return an7581_wifi_mt7996_packet_queue_platform_initialize(
      &pipeline->packet_queue, &queue_config);
}

static enum npu_runtime_result initialize_host_rx(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  uint32_t band;

  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    struct an7581_wifi_mt7996_host_rx_config host_rx_config = {
        .memory = config->memory.host_rx[band],
        .vdma_poll_limit = config->vdma_poll_limit,
    };
    enum npu_runtime_result status;

    if (band == 0U && config->band0_diagnostic_counters != NULL) {
      host_rx_config.diagnostic_counters.enqueue_attempts =
          &config->band0_diagnostic_counters->host_rx_enqueue_attempts;
      host_rx_config.diagnostic_counters.ring_full =
          &config->band0_diagnostic_counters->host_rx_ring_full;
    } else if (band == 1U && config->band1_diagnostic_counters != NULL) {
      host_rx_config.diagnostic_counters.enqueue_attempts =
          &config->band1_diagnostic_counters->host_rx_enqueue_attempts;
      host_rx_config.diagnostic_counters.ring_full =
          &config->band1_diagnostic_counters->host_rx_ring_full;
    }
    if (config->packet_pool->diagnostic_counters != NULL) {
      host_rx_config.diagnostic_counters.fallback_length_uses =
          &config->packet_pool->diagnostic_counters
               ->host_rx_fallback_length_uses;
      host_rx_config.diagnostic_counters.normal_length_uses =
          &config->packet_pool->diagnostic_counters
               ->host_rx_descriptor_length_uses;
      host_rx_config.diagnostic_counters.descriptors_built =
          &config->packet_pool->diagnostic_counters->host_rx_descriptors_built;
    }
    status = an7581_wifi_mt7996_host_rx_platform_initialize(
        &pipeline->host_rx[band], &host_rx_config);

    if (status != NPU_RUNTIME_SUCCESS)
      return status;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result initialize_packet_consumers(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  uint32_t band;

  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    struct an7581_wifi_mt7996_packet_queue_consumer_config consumer_config;
    volatile struct npu_wifi_mt7996_band0_diagnostic_counters *band0_counters =
        band == 0U ? config->band0_diagnostic_counters : NULL;
    volatile struct npu_wifi_mt7996_band1_diagnostic_counters *band1_counters =
        band == 1U ? config->band1_diagnostic_counters : NULL;
    enum npu_runtime_result status;

    pipeline->forward_contexts[band].pipeline = pipeline;
    pipeline->forward_contexts[band].band = band;
    consumer_config = (struct an7581_wifi_mt7996_packet_queue_consumer_config){
        .memory = config->memory.packet_consumers[band],
        .packet_pool = config->packet_pool,
        .forward = forward_packet,
        .forward_context = &pipeline->forward_contexts[band],
        .diagnostic_counters =
            {
                .entries_retired =
                    band0_counters != NULL
                        ? &band0_counters->packet_queue_entries_retired
                    : band1_counters != NULL
                        ? &band1_counters->packet_queue_entries_retired
                        : NULL,
                .consume_attempts =
                    band0_counters != NULL
                        ? &band0_counters->packet_queue_consume_attempts
                    : band1_counters != NULL
                        ? &band1_counters->packet_queue_consume_attempts
                        : NULL,
                .invalid_packet_ids =
                    band0_counters != NULL
                        ? &band0_counters->packet_queue_invalid_packet_ids
                    : band1_counters != NULL
                        ? &band1_counters->packet_queue_invalid_packet_ids
                        : NULL,
                .zero_lengths = band0_counters != NULL
                                    ? &band0_counters->packet_queue_zero_lengths
                                : band1_counters != NULL
                                    ? &band1_counters->packet_queue_zero_lengths
                                    : NULL,
                .packets_forwarded =
                    band0_counters != NULL
                        ? &band0_counters->packet_queue_packets_forwarded
                    : band1_counters != NULL
                        ? &band1_counters->packet_queue_packets_forwarded
                        : NULL,
                .forward_failures =
                    band0_counters != NULL
                        ? &band0_counters->packet_queue_forward_failures
                    : band1_counters != NULL
                        ? &band1_counters->packet_queue_forward_failures
                        : NULL,
            },
        .consumer = config->packet_queue_consumers[band],
    };
    status = an7581_wifi_mt7996_packet_queue_consumer_platform_initialize(
        &pipeline->packet_consumers[band], &consumer_config);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result initialize_fragment_consumer(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  const struct an7581_wifi_mt7996_packet_queue_consumer_memory *packet_memory =
      &config->memory.packet_consumers[1];
  const struct npu_wifi_mt7996_fragment_queue_consumer_config consumer_config =
      {
          .entries = config->memory.secondary_fragment_entries,
          .packet_cached_memory = packet_memory->packet_cached_memory,
          .operations =
              {
                  .forward_packet = forward_fragment,
                  .release_packet = release_fragment_packet,
              },
          .diagnostic_counters = config->band1_diagnostic_counters,
          .retry_limit = config->error_retry_count,
          .delay = fragment_delay,
          .operation_context = pipeline,
          .entry_memory_size =
              config->memory.secondary_fragment_entry_memory_size,
          .packet_memory_size = packet_memory->packet_memory_size,
          .packet_physical_base = packet_memory->packet_physical_base,
          .packet_id_limit = packet_memory->packet_id_limit,
      };

  return npu_wifi_mt7996_fragment_queue_consumer_initialize(
      &pipeline->fragment_consumer, &consumer_config);
}

static enum npu_runtime_result map_host_tx_ring(
    void *context, uint32_t physical_address, size_t size,
    volatile struct npu_wifi_mt7996_host_tx_descriptor **descriptors) {
  uint32_t local_address;

  (void)context;
  if (descriptors == NULL || size == 0U || size > UINT32_MAX ||
      !an7581_dma_buffer_map(physical_address, (uint32_t)size, sizeof(uint32_t),
                             &local_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  *descriptors =
      (volatile struct npu_wifi_mt7996_host_tx_descriptor *)(uintptr_t)
          local_address;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
transfer_host_tx_txwi(void *context, const volatile uint8_t *source_txwi,
                      uint32_t destination_address, uint16_t *record_token) {
  struct an7581_wifi_mt7996_completion_pipeline *pipeline = context;
  uint32_t local_destination;
  uint32_t source_address;
  enum npu_runtime_result status;

  if (pipeline == NULL || source_txwi == NULL || record_token == NULL ||
      (uintptr_t)source_txwi > UINT32_MAX)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  source_address =
      ((uint32_t)(uintptr_t)source_txwi & AN7581_DMA_PHYSICAL_MASK) |
      AN7581_WIFI_MT7996_HOST_TX_VDMA_ALIAS;
  status = an7581_vdma_copy(AN7581_WIFI_MT7996_HOST_TX_VDMA_CHANNEL,
                            source_address, destination_address,
                            NPU_WIFI_MT7996_HOST_TX_TXWI_COPY_SIZE,
                            pipeline->vdma_poll_limit);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  local_destination = an7581_dma_local_alias(destination_address);
  an7581_dma_memory_barrier();
  *record_token =
      *(const volatile uint16_t
            *)(uintptr_t)(local_destination +
                          AN7581_WIFI_MT7996_HOST_TX_TXWI_TOKEN_OFFSET);
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result initialize_host_tx_consumer(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  struct npu_wifi_mt7996_host_tx_ring_consumer_config consumer_config;
  uint32_t band;

  (void)npu_memset(&consumer_config, 0U, sizeof(consumer_config));
  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    consumer_config.band[band].host_ring =
        (volatile struct npu_wifi_tx_ring_registers *)(uintptr_t)
            AN7581_NPU_WLAN_TX_RING(AN7581_WIFI_MT7996_HOST_TX_FIRST_RING +
                                    band);
    consumer_config.band[band].destination_descriptors =
        config->memory.host_tx_destinations[band];
  }
  if (config->band0_diagnostic_counters != NULL) {
    consumer_config.band[0].diagnostic_counters.descriptor_attempts =
        &config->band0_diagnostic_counters->host_tx_descriptor_attempts;
    consumer_config.band[0].diagnostic_counters.destination_full =
        &config->band0_diagnostic_counters->host_tx_destination_full;
    consumer_config.band[0].diagnostic_counters.budget_exhaustions =
        &config->band0_diagnostic_counters->host_tx_budget_exhaustions;
  }
  if (config->band1_diagnostic_counters != NULL) {
    consumer_config.band[1].diagnostic_counters.descriptor_attempts =
        &config->band1_diagnostic_counters->host_tx_descriptor_attempts;
    consumer_config.band[1].diagnostic_counters.destination_full =
        &config->band1_diagnostic_counters->host_tx_destination_full;
    consumer_config.band[1].diagnostic_counters.budget_exhaustions =
        &config->band1_diagnostic_counters->host_tx_budget_exhaustions;
  }
  consumer_config.map_ring = map_host_tx_ring;
  consumer_config.transfer_txwi = transfer_host_tx_txwi;
  consumer_config.operation_context = pipeline;
  consumer_config.destination_descriptor_count =
      NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT;
  return npu_wifi_mt7996_host_tx_ring_consumer_initialize(
      &pipeline->host_tx_consumer, &consumer_config);
}

static enum npu_runtime_result initialize_tx_done(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  const struct an7581_wifi_mt7996_tx_done_config tx_done_config = {
      .memory = config->memory.tx_done,
      .packet_pool = config->packet_pool,
      .enqueue = enqueue_completion,
      .enqueue_context = pipeline,
      .records_processed_counter =
          config->band0_diagnostic_counters != NULL
              ? &config->band0_diagnostic_counters->tx_done_records_processed
              : NULL,
      .invalid_record_type_counter =
          config->band1_diagnostic_counters != NULL
              ? &config->band1_diagnostic_counters->tx_done_invalid_record_types
              : NULL,
  };

  return an7581_wifi_mt7996_tx_done_platform_initialize(&pipeline->tx_done,
                                                        &tx_done_config);
}

static enum npu_runtime_result initialize_runtime(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  const struct an7581_wifi_mt7996_completion_runtime_config runtime_config = {
      .tx_done = &pipeline->tx_done,
      .packet_consumers =
          {
              &pipeline->packet_consumers[0],
              &pipeline->packet_consumers[1],
          },
      .fragment_consumer = &pipeline->fragment_consumer,
      .host_tx_consumer = &pipeline->host_tx_consumer,
      .readiness = config->readiness,
      .tx_done_budget = config->tx_done_budget,
      .band0_budget = config->band0_budget,
  };

  return an7581_wifi_mt7996_completion_runtime_initialize(&pipeline->runtime,
                                                          &runtime_config);
}

enum npu_runtime_result an7581_wifi_mt7996_completion_pipeline_initialize(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config) {
  enum npu_runtime_result status;

  if (pipeline == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (pipeline->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->packet_pool == NULL || !config->packet_pool->initialized ||
      config->vdma_poll_limit == 0U ||
      config->readiness.offload_initialized == NULL ||
      ((uintptr_t)config->readiness.offload_initialized &
       (sizeof(uint32_t) - 1U)) != 0U ||
      config->readiness.tx_done_enabled == NULL ||
      config->readiness.tx_configuration_state == NULL ||
      config->readiness.tx_done_activity == NULL ||
      config->readiness.rx_ring_enabled == NULL ||
      ((uintptr_t)config->readiness.rx_ring_enabled &
       (sizeof(uint32_t) - 1U)) != 0U ||
      config->readiness.rx_configuration_state == NULL ||
      config->readiness.host_rx_rings_ready == NULL ||
      config->tx_done_budget == 0U ||
      config->tx_done_budget > NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT ||
      config->band0_budget == 0U ||
      config->band0_budget > NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET ||
      (uint32_t)config->packet_queue_producer >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)config->packet_queue_consumers[0] >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)config->packet_queue_consumers[1] >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      !memory_is_consistent(&config->memory))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(pipeline, 0U, sizeof(*pipeline));
  pipeline->packet_pool = config->packet_pool;
  pipeline->vdma_poll_limit = config->vdma_poll_limit;
  status = initialize_packet_queue(pipeline, config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_host_rx(pipeline, config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_packet_consumers(pipeline, config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_fragment_consumer(pipeline, config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_host_tx_consumer(pipeline, config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_tx_done(pipeline, config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_runtime(pipeline, config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  an7581_dma_memory_barrier();
  pipeline->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_wifi_mt7996_completion_pipeline_refresh_host_rx_memory(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline) {
  struct an7581_wifi_mt7996_host_rx_memory
      memory[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  enum npu_runtime_result status;
  uint32_t band;

  if (pipeline == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pipeline->initialized)
    return NPU_RUNTIME_REJECTED;

  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    status = an7581_wifi_mt7996_host_rx_memory_resolve(band, &memory[band]);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
  }
  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    status = npu_wifi_mt7996_host_rx_rebind_memory(
        &pipeline->host_rx[band].service, memory[band].descriptors,
        memory[band].producer, memory[band].descriptor_memory_size);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
  }
  return NPU_RUNTIME_SUCCESS;
}

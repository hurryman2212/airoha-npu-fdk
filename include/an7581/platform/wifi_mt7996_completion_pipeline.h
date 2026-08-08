/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_COMPLETION_PIPELINE_H
#define AN7581_WIFI_MT7996_COMPLETION_PIPELINE_H

#include "an7581/platform/wifi_mt7996_completion_runtime.h"
#include "an7581/platform/wifi_mt7996_host_rx.h"
#include "an7581/platform/wifi_mt7996_packet_queue.h"

#define AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART UINT32_C(4)

struct an7581_wifi_mt7996_completion_pipeline_memory {
  struct an7581_wifi_mt7996_tx_done_memory tx_done;
  struct an7581_wifi_mt7996_packet_queue_memory packet_queue;
  struct an7581_wifi_mt7996_packet_queue_consumer_memory
      packet_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct an7581_wifi_mt7996_host_rx_memory
      host_rx[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  volatile struct npu_wifi_tx_packet_descriptor
      *host_tx_destinations[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  volatile struct npu_wifi_mt7996_fragment_queue_entry
      *secondary_fragment_entries;
  size_t secondary_fragment_entry_memory_size;
};

struct an7581_wifi_mt7996_completion_pipeline_config {
  struct an7581_wifi_mt7996_completion_pipeline_memory memory;
  struct npu_wifi_packet_id_pool *packet_pool;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  const volatile uint16_t *error_retry_count;
  struct npu_wifi_mt7996_completion_readiness readiness;
  uint32_t vdma_poll_limit;
  uint32_t tx_done_budget;
  uint32_t band0_budget;
  uint16_t packet_queue_producer;
  uint16_t packet_queue_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
};

struct an7581_wifi_mt7996_completion_pipeline;

struct an7581_wifi_mt7996_completion_forward_context {
  struct an7581_wifi_mt7996_completion_pipeline *pipeline;
  uint32_t band;
};

struct an7581_wifi_mt7996_completion_pipeline {
  struct an7581_wifi_mt7996_packet_queue_platform packet_queue;
  struct an7581_wifi_mt7996_host_rx_platform
      host_rx[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct an7581_wifi_mt7996_packet_queue_consumer_platform
      packet_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_mt7996_fragment_queue_consumer fragment_consumer;
  struct npu_wifi_mt7996_host_tx_ring_consumer host_tx_consumer;
  struct an7581_wifi_mt7996_tx_done_platform tx_done;
  struct an7581_wifi_mt7996_completion_runtime runtime;
  struct an7581_wifi_mt7996_completion_forward_context
      forward_contexts[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_packet_id_pool *packet_pool;
  uint32_t vdma_poll_limit;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_completion_pipeline_memory_resolve(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_completion_pipeline_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_completion_pipeline_initialize(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline,
    const struct an7581_wifi_mt7996_completion_pipeline_config *config);
enum npu_runtime_result
an7581_wifi_mt7996_completion_pipeline_refresh_host_rx_memory(
    struct an7581_wifi_mt7996_completion_pipeline *pipeline);

#endif

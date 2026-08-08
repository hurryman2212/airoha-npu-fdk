/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_COMPLETION_RUNTIME_H
#define NPU_WIFI_MT7996_COMPLETION_RUNTIME_H

#include "an7581/services/wifi/mt7996_fragment_queue_consumer.h"
#include "an7581/services/wifi/mt7996_host_tx_ring_consumer.h"

#define NPU_WIFI_MT7996_COMPLETION_BAND_COUNT UINT32_C(2)
#define NPU_WIFI_MT7996_COMPLETION_READY_STATE UINT8_C(3)
#define NPU_WIFI_MT7996_COMPLETION_HOST_RX_READY UINT8_C(1)
#define NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET UINT32_C(0x100)
#define NPU_WIFI_MT7996_COMPLETION_SECONDARY_BUDGET UINT32_C(1)

struct npu_wifi_mt7996_completion_readiness {
  const volatile uint32_t *offload_initialized;
  const volatile uint8_t *tx_done_enabled;
  const volatile uint8_t *tx_configuration_state;
  volatile uint8_t *tx_done_activity;
  const volatile uint32_t *rx_ring_enabled;
  const volatile uint8_t *rx_configuration_state;
  const volatile uint8_t *host_rx_rings_ready;
};

struct npu_wifi_mt7996_completion_runtime_config {
  struct npu_wifi_mt7996_tx_done *tx_done;
  struct npu_wifi_mt7996_packet_queue_consumer
      *packet_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_mt7996_fragment_queue_consumer *fragment_consumer;
  struct npu_wifi_mt7996_host_tx_ring_consumer *host_tx_consumer;
  struct npu_wifi_mt7996_completion_readiness readiness;
  uint32_t tx_done_budget;
  uint32_t packet_queue_budgets[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
};

struct npu_wifi_mt7996_completion_runtime {
  struct npu_wifi_mt7996_tx_done *tx_done;
  struct npu_wifi_mt7996_packet_queue_consumer
      *packet_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_mt7996_fragment_queue_consumer *fragment_consumer;
  struct npu_wifi_mt7996_host_tx_ring_consumer *host_tx_consumer;
  struct npu_wifi_mt7996_completion_readiness readiness;
  uint32_t tx_done_budget;
  uint32_t packet_queue_budgets[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  uint32_t tx_done_step_count;
  uint32_t packet_queue_step_count;
  bool initialized;
};

struct npu_wifi_mt7996_completion_wait_state {
  bool waiting_for_offload;
  bool waiting_for_tx_done;
  bool waiting_for_tx_configuration;
  bool waiting_for_rx_ring;
  bool waiting_for_rx_configuration;
  bool waiting_for_host_rx;
};

struct npu_wifi_mt7996_completion_tx_done_result {
  struct npu_wifi_mt7996_tx_done_result service;
  struct npu_wifi_mt7996_completion_wait_state wait;
  bool pending_work;
  bool idle;
  bool should_backoff;
};

struct npu_wifi_mt7996_completion_packet_queue_result {
  struct npu_wifi_mt7996_host_tx_ring_consumer_result
      host_tx[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_mt7996_packet_queue_consumer_result
      bands[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_mt7996_fragment_queue_consumer_result fragment;
  struct npu_wifi_mt7996_completion_wait_state wait;
  enum npu_runtime_result first_failure;
  uint32_t processed;
  uint32_t forwarded;
  uint32_t host_tx_staged;
  bool pending_work;
  bool idle;
  bool should_backoff;
};

enum npu_runtime_result npu_wifi_mt7996_completion_runtime_initialize(
    struct npu_wifi_mt7996_completion_runtime *runtime,
    const struct npu_wifi_mt7996_completion_runtime_config *config);
enum npu_runtime_result npu_wifi_mt7996_completion_runtime_step_tx_done(
    struct npu_wifi_mt7996_completion_runtime *runtime,
    struct npu_wifi_mt7996_completion_tx_done_result *result);
enum npu_runtime_result npu_wifi_mt7996_completion_runtime_step_packet_queues(
    struct npu_wifi_mt7996_completion_runtime *runtime,
    struct npu_wifi_mt7996_completion_packet_queue_result *result);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_COMPLETION_RUNTIME_H
#define AN7581_WIFI_MT7996_COMPLETION_RUNTIME_H

#include "an7581/platform/wifi_mt7996_packet_queue_consumer.h"
#include "an7581/platform/wifi_mt7996_tx_done.h"
#include "an7581/services/wifi/mt7996_completion_runtime.h"

struct an7581_wifi_mt7996_runtime_readiness_state {
  volatile uint32_t offload_initialized;
  volatile uint32_t rx_ring_enabled;
  volatile uint8_t tx_done_enabled;
  volatile uint8_t tx_configuration_state;
  volatile uint8_t tx_done_activity;
  volatile uint8_t rx_configuration_state;
  volatile uint8_t host_rx_rings_ready;
};

struct an7581_wifi_mt7996_completion_runtime_config {
  struct an7581_wifi_mt7996_tx_done_platform *tx_done;
  struct an7581_wifi_mt7996_packet_queue_consumer_platform
      *packet_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  struct npu_wifi_mt7996_fragment_queue_consumer *fragment_consumer;
  struct npu_wifi_mt7996_host_tx_ring_consumer *host_tx_consumer;
  struct npu_wifi_mt7996_completion_readiness readiness;
  uint32_t tx_done_budget;
  uint32_t band0_budget;
};

struct an7581_wifi_mt7996_completion_runtime {
  struct npu_wifi_mt7996_completion_runtime service;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_runtime_readiness_initialize(
    struct an7581_wifi_mt7996_runtime_readiness_state *state);
enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_offload(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool initialized);
enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_tx_state(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool enabled,
    bool configured);
enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_rx_state(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool ring_enabled,
    bool configured);
enum npu_runtime_result an7581_wifi_mt7996_runtime_publish_host_rx_state(
    struct an7581_wifi_mt7996_runtime_readiness_state *state, bool ready);
enum npu_runtime_result an7581_wifi_mt7996_completion_readiness_resolve(
    struct an7581_wifi_mt7996_runtime_readiness_state *state,
    struct npu_wifi_mt7996_completion_readiness *readiness);
enum npu_runtime_result an7581_wifi_mt7996_completion_runtime_initialize(
    struct an7581_wifi_mt7996_completion_runtime *platform,
    const struct an7581_wifi_mt7996_completion_runtime_config *config);

#endif

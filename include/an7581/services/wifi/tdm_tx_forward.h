/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TDM_TX_FORWARD_H
#define NPU_WIFI_TDM_TX_FORWARD_H

#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/tdm_rx.h"
#include "an7581/services/wifi/tx_buffer_space.h"
#include "an7581/services/wifi/tx_ring.h"

#define NPU_WIFI_TDM_TX_BAND_COUNT NPU_WIFI_MT7996_TX_BAND_COUNT
#define NPU_WIFI_TDM_TX_BAND0_DESCRIPTOR_COUNT                                 \
  NPU_WIFI_MT7996_TX_BAND0_DESCRIPTOR_COUNT
#define NPU_WIFI_TDM_TX_SECONDARY_DESCRIPTOR_COUNT                             \
  NPU_WIFI_MT7996_TX_SECONDARY_DESCRIPTOR_COUNT
#define NPU_WIFI_TDM_TX_DESCRIPTOR_CONTROL UINT32_C(0x40800000)
#define NPU_WIFI_TDM_TX_DESCRIPTOR_ADDRESS_MASK UINT32_C(0x1fffffff)
#define NPU_WIFI_TDM_TX_RECORD_TOKEN_BASE UINT32_C(0x00000180)
#define NPU_WIFI_TDM_TX_RECORD_ROUTE_BASE UINT32_C(0x00210000)
#define NPU_WIFI_TDM_TX_RECORD_WCID_SENTINEL UINT32_C(0x0fff0008)
#define NPU_WIFI_TDM_TX_RECORD_WCID_SUFFIX UINT32_C(0x00000008)
#define NPU_WIFI_TDM_TX_CURRENT_WAIT_LIMIT UINT32_C(1000)
#define NPU_WIFI_TDM_TX_PUBLISH_RETRY_LIMIT UINT32_C(5)
#define NPU_WIFI_TDM_TX_RETRY_DELAY UINT32_C(10000)

typedef void (*npu_wifi_tdm_tx_retry_delay)(void *context, uint32_t iterations);

struct npu_wifi_tdm_tx_diagnostic_counters {
  volatile uint32_t *current_descriptor_waits;
  volatile uint32_t *descriptor_publish_retries;
  volatile uint32_t *lookahead_descriptor_waits;
};

struct npu_wifi_tdm_tx_band_config {
  volatile struct npu_wifi_tx_descriptor *descriptors;
  volatile struct npu_wifi_tx_buffer_space_record *records;
  volatile struct npu_wifi_tx_ring_registers *registers;
  uint32_t record_physical_base;
  uint32_t descriptor_count;
  struct npu_wifi_tdm_tx_diagnostic_counters diagnostic_counters;
};

struct npu_wifi_tdm_tx_forward_config {
  struct npu_wifi_tdm_tx_band_config band[NPU_WIFI_TDM_TX_BAND_COUNT];
  struct npu_wifi_tx_producer_state *producer_state;
  npu_wifi_tdm_tx_retry_delay delay;
  void *delay_context;
  const volatile bool *stop_requested;
};

struct npu_wifi_tdm_tx_forward {
  struct npu_wifi_tdm_tx_band_config band[NPU_WIFI_TDM_TX_BAND_COUNT];
  struct npu_wifi_tx_producer_state *producer_state;
  struct npu_wifi_tx_producer_state local_producer_state;
  npu_wifi_tdm_tx_retry_delay delay;
  void *delay_context;
  const volatile bool *stop_requested;
  uint8_t last_band;
  uint32_t forwarded_packet_count;
  uint32_t full_count;
  uint32_t cpu_index_publish_count;
  bool last_band_valid;
  bool initialized;
};

enum npu_runtime_result npu_wifi_tdm_tx_forward_initialize(
    struct npu_wifi_tdm_tx_forward *forwarder,
    const struct npu_wifi_tdm_tx_forward_config *config);
enum npu_runtime_result
npu_wifi_tdm_tx_forward_dispatch(void *context, uint32_t ring_index,
                                 const struct npu_wifi_tdm_rx_packet *packet);
enum npu_runtime_result npu_wifi_tdm_tx_forward_publish(void *context);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_DONE_DESCRIPTORS_H
#define NPU_WIFI_TX_DONE_DESCRIPTORS_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_TX_DONE_MT7996_PACKET_ID_MAP_ADDRESS UINT32_C(0x3e8adc70)

typedef enum npu_runtime_result (*npu_wifi_tx_done_force_reset_operation)(
    void *context);

struct npu_wifi_tx_done_descriptor_operations {
  struct npu_wifi_rx_buffer_operations packet_ids;
  npu_wifi_tx_done_force_reset_operation force_reset_token_ids;
};

struct npu_wifi_tx_done_descriptor_config {
  uint32_t packet_buffer_base;
  void *descriptor_memory;
  size_t descriptor_memory_size;
  uint16_t *packet_ids;
  uint32_t packet_id_capacity;
  struct npu_wifi_tx_done_descriptor_operations operations;
  void *packet_id_context;
  void *force_reset_context;
};

struct npu_wifi_tx_done_descriptor_state {
  uint32_t producer;
  uint16_t descriptor_count;
  bool ready;
};

enum npu_runtime_result npu_wifi_tx_done_descriptors_initialize(
    struct npu_wifi_tx_done_descriptor_state *state,
    const struct npu_wifi_tx_done_descriptor_config *config,
    uint32_t descriptor_count);

#endif

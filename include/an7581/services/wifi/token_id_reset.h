/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TOKEN_ID_RESET_H
#define NPU_WIFI_TOKEN_ID_RESET_H

#include "an7581/services/wifi/tdm_rx.h"

#define NPU_WIFI_TOKEN_ID_TDM_RING_COUNT NPU_WIFI_TDM_RX_RING_COUNT
#define NPU_WIFI_TOKEN_ID_TDM_RING_ENTRY_COUNT NPU_WIFI_TDM_RX_RING_ENTRY_COUNT
#define NPU_WIFI_TOKEN_ID_TDM_DESCRIPTOR_SIZE NPU_WIFI_TDM_RX_DESCRIPTOR_SIZE
#define NPU_WIFI_TOKEN_ID_RESET_SCRATCH_COUNT UINT32_C(0x800)
#define NPU_WIFI_TOKEN_ID_PACKET_STRIDE UINT32_C(0x800)
#define NPU_WIFI_TOKEN_ID_UNUSED UINT16_C(0xffff)
#define NPU_WIFI_TOKEN_ID_STATE_TDM UINT16_C(3)

struct npu_wifi_token_id_reset_config {
  const volatile struct an7581_qdma_descriptor
      *tdm_rings[NPU_WIFI_TOKEN_ID_TDM_RING_COUNT];
  volatile uint16_t *scratch_entries;
  volatile uint16_t *token_states;
  uint32_t packet_buffer_base;
  uint32_t scratch_entry_count;
  uint32_t token_state_count;
};

enum npu_runtime_result npu_wifi_token_id_collect_tdm(
    const struct npu_wifi_token_id_reset_config *config, uint32_t token_count,
    uint32_t *collected_count);
enum npu_runtime_result npu_wifi_token_id_pool_force_reset(
    struct npu_wifi_packet_id_pool *pool,
    const struct npu_wifi_token_id_reset_config *config);

#endif

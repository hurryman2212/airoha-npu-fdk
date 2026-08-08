/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_NPU_INFORMATION_H
#define NPU_WIFI_NPU_INFORMATION_H

#include "an7581/services/wifi/rx_ring.h"

struct npu_wifi_npu_information_state {
  uint16_t interface_0_consumer_index;
  uint16_t interface_2_consumer_index;
  uint32_t worker_status;
  uint8_t inode_phase;
  uint8_t mt7996_fast_path_gate;
  uint8_t mt7996_tx_done_gate;
};

bool npu_wifi_npu_information_query(
    const struct npu_wifi_npu_information_state *state, uint32_t interface,
    uint32_t *information);

#endif

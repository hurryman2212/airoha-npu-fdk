/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/npu_information.h"

static uint32_t
query_worker_information(const struct npu_wifi_npu_information_state *state) {
  uint32_t gate_closed =
      state->mt7996_fast_path_gate == 0U || state->mt7996_tx_done_gate == 0U;

  return state->worker_status | gate_closed;
}

bool npu_wifi_npu_information_query(
    const struct npu_wifi_npu_information_state *state, uint32_t interface,
    uint32_t *information) {
  if (state == NULL || information == NULL)
    return false;

  *information = 0U;
  if (interface == 0U) {
    *information = state->interface_0_consumer_index;
    return true;
  }
  if (interface == 2U) {
    *information = state->interface_2_consumer_index;
    return true;
  }

  if (interface != 3U)
    return false;

  *information = query_worker_information(state);
  return true;
}

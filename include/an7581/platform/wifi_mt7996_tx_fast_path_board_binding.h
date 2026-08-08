/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_TX_FAST_PATH_BOARD_BINDING_H
#define AN7581_WIFI_MT7996_TX_FAST_PATH_BOARD_BINDING_H

#include "an7581/platform/board_stop.h"
#include "an7581/platform/wifi_mt7996_tx_fast_path_lifecycle.h"

#define AN7581_WIFI_MT7996_TX_FAST_PATH_DEFAULT_VDMA_POLL_LIMIT UINT32_C(1024)

struct an7581_wifi_mt7996_tx_fast_path_board_binding {
  an7581_wifi_mt7996_tx_fast_path_worker_wake wake_worker;
  void *wake_context;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  uint32_t vdma_poll_limit;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_tx_fast_path_board_configuration {
  struct an7581_wifi_mt7996_tx_fast_path_platform *platform;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  bool activation_allowed;
};

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_board_binding_resolve(
    const struct an7581_wifi_mt7996_tx_fast_path_board_binding *binding,
    struct an7581_core2_dispatch *dispatch,
    struct npu_wifi_sram_allocator *shared_allocator,
    struct npu_wifi_packet_id_pool *shared_packet_pool,
    struct an7581_wifi_mt7996_runtime_readiness_state *readiness,
    volatile struct npu_wifi_mt7996_band0_diagnostic_counters
        *band0_diagnostic_counters,
    volatile struct npu_wifi_mt7996_band1_diagnostic_counters
        *band1_diagnostic_counters,
    struct an7581_wifi_mt7996_tx_fast_path_platform *platform,
    struct an7581_wifi_mt7996_tx_fast_path_board_configuration *configuration);

#endif

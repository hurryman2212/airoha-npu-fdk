/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_COMPLETION_BOARD_BINDING_H
#define AN7581_WIFI_MT7996_COMPLETION_BOARD_BINDING_H

#include "an7581/platform/board_stop.h"
#include "an7581/platform/wifi_mt7996_completion_lifecycle.h"

#define AN7581_WIFI_MT7996_COMPLETION_DEFAULT_VDMA_POLL_LIMIT UINT32_C(1024)

struct an7581_wifi_mt7996_completion_board_binding {
  an7581_wifi_mt7996_completion_worker_wake wake_workers;
  void *wake_context;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  uint32_t vdma_poll_limit;
  uint32_t tx_done_budget;
  uint32_t band0_budget;
  uint16_t packet_queue_producer;
  uint16_t packet_queue_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  bool activation_allowed;
};

struct an7581_wifi_mt7996_completion_board_configuration {
  an7581_wifi_mt7996_completion_worker_wake wake_workers;
  void *wake_context;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  uint32_t vdma_poll_limit;
  uint32_t tx_done_budget;
  uint32_t band0_budget;
  uint16_t packet_queue_producer;
  uint16_t packet_queue_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  bool activation_allowed;
};

enum npu_runtime_result an7581_wifi_mt7996_completion_board_binding_resolve(
    const struct an7581_wifi_mt7996_completion_board_binding *binding,
    struct an7581_wifi_mt7996_completion_board_configuration *configuration);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_RX_REFILL_BOARD_BINDING_H
#define AN7581_WIFI_MT7996_RX_REFILL_BOARD_BINDING_H

#include "an7581/platform/board_stop.h"
#include "an7581/platform/wifi_mt7996_rx_refill_lifecycle.h"

struct an7581_wifi_mt7996_rx_refill_board_binding {
  const struct an7581_wifi_mt7996_rx_refill_operations *operations;
  void *operation_context;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_rx_refill_board_configuration {
  const struct an7581_wifi_mt7996_rx_refill_operations *operations;
  void *operation_context;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  bool activation_allowed;
};

enum npu_runtime_result an7581_wifi_mt7996_rx_refill_board_binding_resolve(
    const struct an7581_wifi_mt7996_rx_refill_board_binding *binding,
    struct an7581_wifi_mt7996_rx_refill_board_configuration *configuration);

#endif

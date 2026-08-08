/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_BOARD_BINDING_H
#define AN7581_TR471_BOARD_BINDING_H

#include "an7581/platform/tr471_runtime_lifecycle.h"

struct an7581_tr471_board_binding {
  an7581_tr471_runtime_dispatch_wake wake_harts;
  void *wake_context;
  uint32_t timer_clock_mhz;
  uint32_t transmit_budget;
  uint32_t receive_budget;
  uint32_t shared_buffer_extent;
  bool activation_allowed;
};

struct an7581_tr471_board_configuration {
  an7581_tr471_runtime_dispatch_wake wake_harts;
  void *wake_context;
  uint32_t timer_clock_mhz;
  uint32_t transmit_budget;
  uint32_t receive_budget;
  uint32_t shared_buffer_extent;
  bool activation_allowed;
};

enum npu_runtime_result an7581_tr471_board_binding_resolve(
    const struct an7581_tr471_board_binding *binding,
    struct an7581_tr471_board_configuration *configuration);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_TIMER_H
#define AN7581_TR471_TIMER_H

#include "an7581/services/tr471/runtime.h"

#define AN7581_TR471_TIMER_CONTROL_HART UINT32_C(0)
#define AN7581_TR471_TIMER_HART UINT32_C(2)
#define AN7581_TR471_RUNTIME_HART UINT32_C(7)
#define AN7581_TR471_TIMER_INTERRUPT_SOURCE UINT32_C(0x12)
#define AN7581_TR471_TIMER_INTERRUPT_PRIORITY UINT32_C(4)
#define AN7581_TR471_TIMER_INTERRUPT_BIT UINT32_C(0x00010000)
#define AN7581_TR471_TIMER_CONTROL_PRESERVE_MASK UINT32_C(0x02000027)
#define AN7581_TR471_TIMER_ENABLE UINT32_C(1)
#define AN7581_TR471_TIMER_RELOAD_OFFSET UINT32_C(4)

struct an7581_tr471_timer {
  struct npu_tr471_state *tr471;
  uint32_t cached_control;
  uint32_t interrupt_count;
  uint32_t unexpected_source_count;
  bool control_cached;
  bool interrupt_registered;
  bool timer_started;
  bool initialized;
};

enum npu_runtime_result
an7581_tr471_timer_initialize(struct an7581_tr471_timer *timer,
                              struct npu_tr471_state *state);
enum npu_runtime_result
an7581_tr471_timer_interrupt_register(struct an7581_tr471_timer *timer,
                                      uint32_t hart_id,
                                      bool activation_allowed);
enum npu_runtime_result
an7581_tr471_timer_start(struct an7581_tr471_timer *timer, uint32_t hart_id,
                         uint32_t clock_mhz, bool activation_allowed);
enum npu_runtime_result
an7581_tr471_timer_handle_interrupt(struct an7581_tr471_timer *timer,
                                    uint32_t source);

#endif

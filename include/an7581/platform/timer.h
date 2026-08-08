/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TIMER_H
#define AN7581_TIMER_H

#include "an7581/platform/memory_map.h"

#define AN7581_LOCAL_TIMER_COUNT 4U
#define AN7581_LOCAL_TIMER_ENABLE UINT32_C(0x00000001)
#define AN7581_LOCAL_TIMER_WATCHDOG_ENABLE (UINT32_C(1) << 25)
#define AN7581_LOCAL_TIMER_CONTROL AN7581_LOCAL_TIMER_BASE
#define AN7581_LOCAL_TIMER_LOAD (AN7581_LOCAL_TIMER_CONTROL + UINT32_C(0x004))
#define AN7581_LOCAL_TIMER_VALUE (AN7581_LOCAL_TIMER_CONTROL + UINT32_C(0x008))

enum an7581_local_timer_period_unit {
  AN7581_LOCAL_TIMER_PERIOD_10_MICROSECONDS = 0,
  AN7581_LOCAL_TIMER_PERIOD_MILLISECONDS,
};

enum an7581_local_timer_control_policy {
  AN7581_LOCAL_TIMER_PRESERVE_WATCHDOG = 0,
  AN7581_LOCAL_TIMER_DISABLE_WATCHDOG_ON_ENABLE,
};

struct an7581_timer_deadline {
  uint32_t reload;
  uint32_t last_value;
  uint32_t remaining_ticks;
  bool active;
};

static inline uint32_t an7581_local_timer_enable_index(uint32_t timer) {
  return timer == 3U ? 5U : timer;
}

static inline uint32_t an7581_local_timer_reload_address(uint32_t timer) {
  return AN7581_LOCAL_TIMER_CONTROL + UINT32_C(0x004) +
         an7581_local_timer_enable_index(timer) * UINT32_C(0x008);
}

bool an7581_cpu_timer_calculate_ticks(uint32_t period_ms, uint32_t clock_mhz,
                                      uint32_t *ticks);
bool an7581_timer_elapsed_ticks(uint32_t reload, uint32_t previous,
                                uint32_t current, uint32_t *elapsed);
bool an7581_local_timer_configure(
    uint32_t timer, uint32_t period, uint32_t clock_mhz, bool enabled,
    enum an7581_local_timer_period_unit period_unit,
    enum an7581_local_timer_control_policy control_policy);
bool an7581_local_timer_deadline_start(struct an7581_timer_deadline *deadline,
                                       uint32_t period_ms, uint32_t clock_mhz);
bool an7581_local_timer_deadline_poll(struct an7581_timer_deadline *deadline,
                                      bool *expired);
bool an7581_local_timer_delay_ms(uint32_t period_ms, uint32_t clock_mhz);

#endif

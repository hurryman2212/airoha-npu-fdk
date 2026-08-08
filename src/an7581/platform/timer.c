/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/timer.h"

#include "an7581/platform/mmio.h"

static bool
local_timer_calculate_ticks(uint32_t period, uint32_t clock_mhz,
                            enum an7581_local_timer_period_unit period_unit,
                            uint32_t *ticks) {
  uint32_t ticks_per_mhz;

  if (period == 0U || clock_mhz == 0U || ticks == NULL)
    return false;

  if (period_unit == AN7581_LOCAL_TIMER_PERIOD_10_MICROSECONDS)
    ticks_per_mhz = 10U;
  else if (period_unit == AN7581_LOCAL_TIMER_PERIOD_MILLISECONDS)
    ticks_per_mhz = 1000U;
  else
    return false;

  if (period > UINT32_MAX / clock_mhz ||
      period * clock_mhz > UINT32_MAX / ticks_per_mhz)
    return false;

  *ticks = period * clock_mhz * ticks_per_mhz;
  return true;
}

bool an7581_local_timer_configure(
    uint32_t timer, uint32_t period, uint32_t clock_mhz, bool enabled,
    enum an7581_local_timer_period_unit period_unit,
    enum an7581_local_timer_control_policy control_policy) {
  uint32_t control;
  uint32_t enable_bit;
  uint32_t ticks = 0U;

  if (timer >= AN7581_LOCAL_TIMER_COUNT ||
      (control_policy != AN7581_LOCAL_TIMER_PRESERVE_WATCHDOG &&
       control_policy != AN7581_LOCAL_TIMER_DISABLE_WATCHDOG_ON_ENABLE) ||
      (enabled &&
       !local_timer_calculate_ticks(period, clock_mhz, period_unit, &ticks)))
    return false;

  enable_bit = UINT32_C(1) << an7581_local_timer_enable_index(timer);
  control = an7581_mmio_read32(AN7581_LOCAL_TIMER_CONTROL);
  if (!enabled) {
    an7581_mmio_write32(AN7581_LOCAL_TIMER_CONTROL, control & ~enable_bit);
    return true;
  }

  an7581_mmio_write32(an7581_local_timer_reload_address(timer), ticks);
  control |= enable_bit;
  if (control_policy == AN7581_LOCAL_TIMER_DISABLE_WATCHDOG_ON_ENABLE)
    control &= ~AN7581_LOCAL_TIMER_WATCHDOG_ENABLE;
  an7581_mmio_write32(AN7581_LOCAL_TIMER_CONTROL, control);
  return true;
}

bool an7581_local_timer_deadline_start(struct an7581_timer_deadline *deadline,
                                       uint32_t period_ms, uint32_t clock_mhz) {
  uint32_t ticks;

  if (deadline == NULL ||
      (an7581_mmio_read32(AN7581_LOCAL_TIMER_CONTROL) &
       AN7581_LOCAL_TIMER_ENABLE) == 0U ||
      !an7581_cpu_timer_calculate_ticks(period_ms, clock_mhz, &ticks))
    return false;

  deadline->reload = an7581_mmio_read32(AN7581_LOCAL_TIMER_LOAD);
  deadline->last_value = an7581_mmio_read32(AN7581_LOCAL_TIMER_VALUE);
  if (deadline->reload == 0U || deadline->last_value > deadline->reload) {
    deadline->active = false;
    return false;
  }

  deadline->remaining_ticks = ticks;
  deadline->active = true;
  return true;
}

bool an7581_local_timer_deadline_poll(struct an7581_timer_deadline *deadline,
                                      bool *expired) {
  uint32_t current;
  uint32_t elapsed;

  if (deadline == NULL || expired == NULL || !deadline->active)
    return false;

  current = an7581_mmio_read32(AN7581_LOCAL_TIMER_VALUE);
  if (!an7581_timer_elapsed_ticks(deadline->reload, deadline->last_value,
                                  current, &elapsed)) {
    deadline->active = false;
    return false;
  }

  deadline->last_value = current;
  if (elapsed >= deadline->remaining_ticks) {
    deadline->remaining_ticks = 0U;
    deadline->active = false;
    *expired = true;
  } else {
    deadline->remaining_ticks -= elapsed;
    *expired = false;
  }

  return true;
}

bool an7581_local_timer_delay_ms(uint32_t period_ms, uint32_t clock_mhz) {
  struct an7581_timer_deadline deadline;
  bool expired;

  if (!an7581_local_timer_deadline_start(&deadline, period_ms, clock_mhz))
    return false;

  do {
    if (!an7581_local_timer_deadline_poll(&deadline, &expired))
      return false;
  } while (!expired);

  return true;
}

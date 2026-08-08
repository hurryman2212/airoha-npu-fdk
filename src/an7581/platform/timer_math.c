/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/timer.h"

bool an7581_cpu_timer_calculate_ticks(uint32_t period_ms, uint32_t clock_mhz,
                                      uint32_t *ticks) {
  uint32_t ticks_per_ms;

  if (ticks == NULL || period_ms == 0U || clock_mhz == 0U ||
      clock_mhz > UINT32_MAX / 1000U)
    return false;

  ticks_per_ms = clock_mhz * 1000U;
  if (period_ms > UINT32_MAX / ticks_per_ms)
    return false;

  *ticks = period_ms * ticks_per_ms;
  return true;
}

bool an7581_timer_elapsed_ticks(uint32_t reload, uint32_t previous,
                                uint32_t current, uint32_t *elapsed) {
  if (elapsed == NULL || reload == 0U || previous > reload || current > reload)
    return false;

  if (previous >= current)
    *elapsed = previous - current;
  else
    *elapsed = (reload - current) + previous;

  return true;
}

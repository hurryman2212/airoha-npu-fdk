/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_SYSTEM_INITIALIZATION_H
#define AN7581_SYSTEM_INITIALIZATION_H

#include "an7581/platform/types.h"

struct an7581_system_clock_rates {
  uint32_t core_mhz;
  uint32_t timer_mhz;
};

bool an7581_system_initialize(struct an7581_system_clock_rates *clock_rates);

#endif

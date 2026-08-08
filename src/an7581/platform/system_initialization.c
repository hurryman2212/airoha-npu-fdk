/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/system_initialization.h"

#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/timer.h"
#include "an7581/platform/uart.h"

#define AN7581_SYSTEM_CORE_CLOCK_DEDICATED_MHZ 100U
#define AN7581_SYSTEM_TIMER_CLOCK_DEDICATED_MHZ 25U
#define AN7581_SYSTEM_LOCAL_TIMER_PERIOD_10_MICROSECONDS 10U

static uint32_t clock_rate_from_configuration(uint32_t configuration) {
  static const uint16_t numerator_mhz[] = {800U, 750U, 720U, 600U};
  uint32_t denominator = (configuration & UINT32_C(0x07)) + 1U;
  uint32_t numerator_index = (configuration >> 6) & UINT32_C(0x03);

  return numerator_mhz[numerator_index] / denominator;
}

static struct an7581_system_clock_rates system_clock_rates_read(void) {
  struct an7581_system_clock_rates rates;

  if (an7581_mmio_read32(AN7581_NPU_MIB_12) != 0U) {
    rates.core_mhz = AN7581_SYSTEM_CORE_CLOCK_DEDICATED_MHZ;
    rates.timer_mhz = AN7581_SYSTEM_TIMER_CLOCK_DEDICATED_MHZ;
  } else {
    rates.core_mhz = clock_rate_from_configuration(
        an7581_mmio_read32(AN7581_NPU_CLOCK_CONFIGURATION));
    rates.timer_mhz = rates.core_mhz / 4U;
  }

  return rates;
}

static void delay_one_millisecond(uint32_t core_clock_mhz) {
#ifndef AN7581_MMIO_TEST
  uint32_t elapsed = 0U;
  uint32_t previous;
  uint32_t current;
  uint32_t target = core_clock_mhz * 1000U;

  __asm__ volatile("csrr %0, mcycle" : "=r"(previous));
  while (elapsed < target) {
    __asm__ volatile("csrr %0, mcycle" : "=r"(current));
    elapsed += current - previous;
    previous = current;
  }
#else
  (void)core_clock_mhz;
#endif
}

bool an7581_system_initialize(struct an7581_system_clock_rates *clock_rates) {
  struct an7581_system_clock_rates rates;
  uint32_t logging_gate;

  if (clock_rates == NULL)
    return false;

  logging_gate = an7581_mmio_read32(AN7581_NPU_MIB_21);
  rates = system_clock_rates_read();
  if (rates.core_mhz == 0U || rates.timer_mhz == 0U)
    return false;

  an7581_mmio_write32(AN7581_NPU_COMMON_STARTUP_CONTROL, UINT32_C(0x10001788));
  delay_one_millisecond(rates.core_mhz);
  an7581_mmio_write32(AN7581_NPU_COMMON_STARTUP_CONTROL, 0U);
  delay_one_millisecond(rates.core_mhz);
  an7581_mmio_write32(AN7581_NPU_COMMON_STARTUP_TRIGGER, 4U);
  an7581_mmio_write32(AN7581_NPU_COMMON_STARTUP_TRIGGER, 1U);
  delay_one_millisecond(rates.core_mhz);
  an7581_mmio_write32(AN7581_NPU_MIB_0, UINT32_MAX);

  an7581_uart_configure();
  an7581_mmio_write32(AN7581_NPU_MIB_21, logging_gate);
  if (!an7581_uart_interrupt_initialize() ||
      !an7581_local_timer_configure(
          0U, AN7581_SYSTEM_LOCAL_TIMER_PERIOD_10_MICROSECONDS, rates.timer_mhz,
          true, AN7581_LOCAL_TIMER_PERIOD_10_MICROSECONDS,
          AN7581_LOCAL_TIMER_PRESERVE_WATCHDOG))
    return false;

  *clock_rates = rates;
  return true;
}

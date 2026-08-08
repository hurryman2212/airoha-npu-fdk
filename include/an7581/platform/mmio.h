/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_MMIO_H
#define AN7581_MMIO_H

#include "an7581/platform/types.h"

#ifdef AN7581_MMIO_TEST

uint32_t an7581_mmio_test_read32(uint32_t address);
void an7581_mmio_test_write32(uint32_t address, uint32_t value);

static inline void an7581_memory_barrier(void) {
  __asm__ volatile("" ::: "memory");
}

static inline uint32_t an7581_mmio_read32(uint32_t address) {
  return an7581_mmio_test_read32(address);
}

static inline void an7581_mmio_write32(uint32_t address, uint32_t value) {
  an7581_mmio_test_write32(address, value);
}

static inline void an7581_enable_machine_external_interrupts(void) {}

static inline void an7581_wait_for_interrupt(void) {}

static inline void an7581_cpu_relax(void) { __asm__ volatile("" ::: "memory"); }

#else

static inline void an7581_memory_barrier(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline uint32_t an7581_mmio_read32(uint32_t address) {
  uint32_t value = *(volatile uint32_t *)(uintptr_t)address;

  an7581_memory_barrier();
  return value;
}

static inline void an7581_mmio_write32(uint32_t address, uint32_t value) {
  an7581_memory_barrier();
  *(volatile uint32_t *)(uintptr_t)address = value;
  an7581_memory_barrier();
}

static inline void an7581_enable_machine_external_interrupts(void) {
  uint32_t machine_external_interrupt = UINT32_C(1) << 11;

  __asm__ volatile("csrs mie, %0"
                   :
                   : "r"(machine_external_interrupt)
                   : "memory");
  __asm__ volatile("csrsi mstatus, 8" ::: "memory");
}

static inline void an7581_wait_for_interrupt(void) { __asm__ volatile("wfi"); }

static inline void an7581_cpu_relax(void) {
  __asm__ volatile("nop" ::: "memory");
}

#endif

#endif

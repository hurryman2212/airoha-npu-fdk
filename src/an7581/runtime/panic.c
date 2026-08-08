/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/runtime/panic.h"

#include "an7581/runtime/crash_dump.h"

enum npu_runtime_result an7581_panic_record(enum an7581_panic_code code,
                                            uint32_t core,
                                            uint32_t program_counter,
                                            uint32_t return_address,
                                            uint32_t stack_pointer) {
  if (code <= AN7581_PANIC_NONE || code >= AN7581_PANIC_CODE_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  return an7581_crash_dump_record_panic(core, (uint32_t)code, program_counter,
                                        return_address, stack_pointer);
}

void an7581_panic(enum an7581_panic_code code) {
#ifdef AN7581_MMIO_TEST
  (void)code;
  __builtin_trap();
#else
  uint32_t core;
  uint32_t program_counter = (uint32_t)(uintptr_t)an7581_panic;
  uint32_t return_address = (uint32_t)(uintptr_t)__builtin_return_address(0);
  uint32_t stack_pointer;

  __asm__ volatile("csrr %0, mhartid" : "=r"(core));
  __asm__ volatile("mv %0, sp" : "=r"(stack_pointer));
  (void)an7581_panic_record(code, core, program_counter, return_address,
                            stack_pointer);
  __asm__ volatile("csrwi mie, 0" ::: "memory");
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  for (;;)
    __asm__ volatile("wfi");
#endif
}

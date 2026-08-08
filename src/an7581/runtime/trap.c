/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/runtime/trap.h"

#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/plic.h"
#include "an7581/runtime/crash_dump.h"

#define AN7581_MACHINE_EXTERNAL_INTERRUPT UINT32_C(0x8000000b)

volatile struct an7581_trap_record g_trap_records[AN7581_NPU_CORE_COUNT];

uint32_t an7581_trap_dispatch(uint32_t cause, uint32_t program_counter,
                              uint32_t trap_value,
                              const struct an7581_trap_frame *frame,
                              uint32_t core) {
  volatile struct an7581_trap_record *record;

  if (core >= AN7581_NPU_CORE_COUNT || frame == NULL)
    return 0U;

  if (cause == AN7581_MACHINE_EXTERNAL_INTERRUPT && an7581_plic_dispatch())
    return program_counter;

  record = &g_trap_records[core];
  record->valid = 0U;
  record->core = core;
  record->cause = cause;
  record->program_counter = program_counter;
  record->trap_value = trap_value;
  record->return_address = frame->return_address;
  record->stack_pointer = frame->stack_pointer;
  record->reserved = 0U;
  an7581_memory_barrier();
  record->valid = AN7581_TRAP_RECORD_VALID;
  an7581_memory_barrier();
  (void)an7581_crash_dump_record_trap(core, cause, program_counter, trap_value,
                                      frame->return_address,
                                      frame->stack_pointer);
  return 0U;
}

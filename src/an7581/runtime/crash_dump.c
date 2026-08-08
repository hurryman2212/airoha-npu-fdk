/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/runtime/crash_dump.h"

#include "an7581/platform/mmio.h"

#define AN7581_CRASH_DUMP_WORD_COUNT (AN7581_CRASH_DUMP_SIZE / sizeof(uint32_t))

volatile struct an7581_crash_dump g_an7581_crash_dump
    __attribute__((section(".crash_dump"), aligned(64), used));

static bool dump_is_valid(void) {
  return g_an7581_crash_dump.header.magic == AN7581_CRASH_DUMP_MAGIC &&
         g_an7581_crash_dump.header.format_version ==
             AN7581_CRASH_DUMP_FORMAT_VERSION &&
         g_an7581_crash_dump.header.total_size == AN7581_CRASH_DUMP_SIZE &&
         g_an7581_crash_dump.header.record_count == AN7581_NPU_CORE_COUNT &&
         g_an7581_crash_dump.header.record_size ==
             sizeof(struct an7581_crash_dump_record) &&
         g_an7581_crash_dump.footer.magic == AN7581_CRASH_DUMP_FOOTER_MAGIC &&
         g_an7581_crash_dump.footer.total_size == AN7581_CRASH_DUMP_SIZE &&
         g_an7581_crash_dump.footer.format_version ==
             AN7581_CRASH_DUMP_FORMAT_VERSION &&
         g_an7581_crash_dump.footer.boot_generation ==
             g_an7581_crash_dump.header.boot_generation;
}

static void dump_clear(void) {
  volatile uint32_t *word = (volatile uint32_t *)&g_an7581_crash_dump;
  uint32_t index;

  for (index = 0U; index < AN7581_CRASH_DUMP_WORD_COUNT; ++index)
    word[index] = 0U;
}

enum npu_runtime_result an7581_crash_dump_initialize(void) {
  uint32_t boot_generation = 1U;

  if (dump_is_valid()) {
    boot_generation = g_an7581_crash_dump.header.boot_generation + 1U;
    if (boot_generation == 0U)
      boot_generation = 1U;
  }

  g_an7581_crash_dump.header.magic = 0U;
  g_an7581_crash_dump.footer.magic = 0U;
  an7581_memory_barrier();
  dump_clear();
  g_an7581_crash_dump.header.format_version = AN7581_CRASH_DUMP_FORMAT_VERSION;
  g_an7581_crash_dump.header.total_size = AN7581_CRASH_DUMP_SIZE;
  g_an7581_crash_dump.header.record_count = AN7581_NPU_CORE_COUNT;
  g_an7581_crash_dump.header.record_size =
      sizeof(struct an7581_crash_dump_record);
  g_an7581_crash_dump.header.boot_generation = boot_generation;
  g_an7581_crash_dump.footer.total_size = AN7581_CRASH_DUMP_SIZE;
  g_an7581_crash_dump.footer.boot_generation = boot_generation;
  g_an7581_crash_dump.footer.format_version = AN7581_CRASH_DUMP_FORMAT_VERSION;
  an7581_memory_barrier();
  g_an7581_crash_dump.footer.magic = AN7581_CRASH_DUMP_FOOTER_MAGIC;
  an7581_memory_barrier();
  g_an7581_crash_dump.header.magic = AN7581_CRASH_DUMP_MAGIC;
  an7581_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t next_record_sequence(uint32_t sequence) {
  uint32_t next = (sequence + 2U) & ~UINT32_C(1);

  return next == 0U ? 2U : next;
}

static enum npu_runtime_result
record_crash(uint32_t core, enum an7581_crash_source source, uint32_t cause,
             uint32_t program_counter, uint32_t trap_value,
             uint32_t return_address, uint32_t stack_pointer,
             uint32_t auxiliary0, uint32_t auxiliary1) {
  volatile struct an7581_crash_dump_record *record;
  uint32_t sequence;

  if (core >= AN7581_NPU_CORE_COUNT || source == AN7581_CRASH_SOURCE_NONE)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!dump_is_valid())
    (void)an7581_crash_dump_initialize();

  record = &g_an7581_crash_dump.record[core];
  sequence = next_record_sequence(record->sequence);
  record->sequence = sequence - 1U;
  an7581_memory_barrier();
  record->valid = 0U;
  record->source = (uint32_t)source;
  record->core = core;
  record->cause = cause;
  record->program_counter = program_counter;
  record->trap_value = trap_value;
  record->return_address = return_address;
  record->stack_pointer = stack_pointer;
  record->auxiliary[0] = auxiliary0;
  record->auxiliary[1] = auxiliary1;
  record->reserved[0] = 0U;
  record->reserved[1] = 0U;
  record->reserved[2] = 0U;
  an7581_memory_barrier();
  record->sequence = sequence;
  an7581_memory_barrier();
  record->valid = AN7581_CRASH_DUMP_RECORD_MAGIC;
  an7581_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_crash_dump_record_trap(uint32_t core, uint32_t cause,
                              uint32_t program_counter, uint32_t trap_value,
                              uint32_t return_address, uint32_t stack_pointer) {
  return record_crash(core, AN7581_CRASH_SOURCE_TRAP, cause, program_counter,
                      trap_value, return_address, stack_pointer, 0U, 0U);
}

enum npu_runtime_result an7581_crash_dump_record_panic(uint32_t core,
                                                       uint32_t panic_code,
                                                       uint32_t program_counter,
                                                       uint32_t return_address,
                                                       uint32_t stack_pointer) {
  return record_crash(core, AN7581_CRASH_SOURCE_PANIC, panic_code,
                      program_counter, 0U, return_address, stack_pointer, 0U,
                      0U);
}

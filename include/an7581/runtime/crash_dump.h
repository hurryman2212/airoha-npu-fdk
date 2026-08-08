/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_RUNTIME_CRASH_DUMP_H
#define AN7581_RUNTIME_CRASH_DUMP_H

#include "an7581/platform/memory_map.h"
#include "an7581/runtime/status.h"

#define AN7581_CRASH_DUMP_REGION_SIZE UINT32_C(0x1000)
#define AN7581_CRASH_DUMP_ADDRESS                                              \
  (AN7581_NPU_FIRMWARE_BASE + AN7581_NPU_FIRMWARE_MAX_SIZE -                   \
   AN7581_CRASH_DUMP_REGION_SIZE)
#define AN7581_CRASH_DUMP_SIZE UINT32_C(512)
#define AN7581_CRASH_DUMP_MAGIC UINT32_C(0x4355504e)
#define AN7581_CRASH_DUMP_FOOTER_MAGIC UINT32_C(0x21444e45)
#define AN7581_CRASH_DUMP_RECORD_MAGIC UINT32_C(0x4452434e)
#define AN7581_CRASH_DUMP_FORMAT_VERSION UINT32_C(1)

enum an7581_crash_source {
  AN7581_CRASH_SOURCE_NONE = 0,
  AN7581_CRASH_SOURCE_TRAP,
  AN7581_CRASH_SOURCE_PANIC,
};

struct an7581_crash_dump_header {
  uint32_t magic;
  uint32_t format_version;
  uint32_t total_size;
  uint32_t record_count;
  uint32_t record_size;
  uint32_t boot_generation;
  uint32_t reserved[2];
};

struct an7581_crash_dump_record {
  uint32_t sequence;
  uint32_t valid;
  uint32_t source;
  uint32_t core;
  uint32_t cause;
  uint32_t program_counter;
  uint32_t trap_value;
  uint32_t return_address;
  uint32_t stack_pointer;
  uint32_t auxiliary[2];
  uint32_t reserved[3];
};

struct an7581_crash_dump_footer {
  uint32_t magic;
  uint32_t total_size;
  uint32_t boot_generation;
  uint32_t format_version;
};

struct an7581_crash_dump {
  struct an7581_crash_dump_header header;
  struct an7581_crash_dump_record record[AN7581_NPU_CORE_COUNT];
  uint32_t reserved[4];
  struct an7581_crash_dump_footer footer;
};

_Static_assert(sizeof(struct an7581_crash_dump_header) == 32U,
               "crash dump header layout changed");
_Static_assert(sizeof(struct an7581_crash_dump_record) == 56U,
               "crash dump record layout changed");
_Static_assert(sizeof(struct an7581_crash_dump_footer) == 16U,
               "crash dump footer layout changed");
_Static_assert(sizeof(struct an7581_crash_dump) == AN7581_CRASH_DUMP_SIZE,
               "crash dump ABI must remain 512 bytes");

extern volatile struct an7581_crash_dump g_an7581_crash_dump;

enum npu_runtime_result an7581_crash_dump_initialize(void);
enum npu_runtime_result
an7581_crash_dump_record_trap(uint32_t core, uint32_t cause,
                              uint32_t program_counter, uint32_t trap_value,
                              uint32_t return_address, uint32_t stack_pointer);
enum npu_runtime_result an7581_crash_dump_record_panic(uint32_t core,
                                                       uint32_t panic_code,
                                                       uint32_t program_counter,
                                                       uint32_t return_address,
                                                       uint32_t stack_pointer);
#endif

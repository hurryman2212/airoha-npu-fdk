/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_RUNTIME_PANIC_H
#define AN7581_RUNTIME_PANIC_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

enum an7581_panic_code {
  AN7581_PANIC_NONE = 0,
  AN7581_PANIC_DATA_IMAGE,
  AN7581_PANIC_CONFIGURATION,
  AN7581_PANIC_PPE_RUNTIME,
  AN7581_PANIC_MAILBOX_RUNTIME,
  AN7581_PANIC_CODE_COUNT,
};

enum npu_runtime_result an7581_panic_record(enum an7581_panic_code code,
                                            uint32_t core,
                                            uint32_t program_counter,
                                            uint32_t return_address,
                                            uint32_t stack_pointer);
void an7581_panic(enum an7581_panic_code code)
    __attribute__((noreturn, noinline));

#endif

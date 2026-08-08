/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_RUNTIME_TRAP_H
#define AN7581_RUNTIME_TRAP_H

#define AN7581_TRAP_FRAME_SIZE 128
#define AN7581_TRAP_FRAME_RA_OFFSET 4
#define AN7581_TRAP_FRAME_SP_OFFSET 8
#define AN7581_TRAP_FRAME_GP_OFFSET 12
#define AN7581_TRAP_FRAME_TP_OFFSET 16
#define AN7581_TRAP_FRAME_T0_OFFSET 20
#define AN7581_TRAP_FRAME_T1_OFFSET 24
#define AN7581_TRAP_FRAME_T2_OFFSET 28
#define AN7581_TRAP_FRAME_S0_OFFSET 32
#define AN7581_TRAP_FRAME_S1_OFFSET 36
#define AN7581_TRAP_FRAME_A0_OFFSET 40
#define AN7581_TRAP_FRAME_A1_OFFSET 44
#define AN7581_TRAP_FRAME_A2_OFFSET 48
#define AN7581_TRAP_FRAME_A3_OFFSET 52
#define AN7581_TRAP_FRAME_A4_OFFSET 56
#define AN7581_TRAP_FRAME_A5_OFFSET 60
#define AN7581_TRAP_FRAME_A6_OFFSET 64
#define AN7581_TRAP_FRAME_A7_OFFSET 68
#define AN7581_TRAP_FRAME_S2_OFFSET 72
#define AN7581_TRAP_FRAME_S3_OFFSET 76
#define AN7581_TRAP_FRAME_S4_OFFSET 80
#define AN7581_TRAP_FRAME_S5_OFFSET 84
#define AN7581_TRAP_FRAME_S6_OFFSET 88
#define AN7581_TRAP_FRAME_S7_OFFSET 92
#define AN7581_TRAP_FRAME_S8_OFFSET 96
#define AN7581_TRAP_FRAME_S9_OFFSET 100
#define AN7581_TRAP_FRAME_S10_OFFSET 104
#define AN7581_TRAP_FRAME_S11_OFFSET 108
#define AN7581_TRAP_FRAME_T3_OFFSET 112
#define AN7581_TRAP_FRAME_T4_OFFSET 116
#define AN7581_TRAP_FRAME_T5_OFFSET 120
#define AN7581_TRAP_FRAME_T6_OFFSET 124

#ifndef __ASSEMBLER__

#include "an7581/platform/memory_map.h"

#define AN7581_TRAP_RECORD_VALID UINT32_C(0x54524150)

struct an7581_trap_frame {
  uint32_t reserved;
  uint32_t return_address;
  uint32_t stack_pointer;
  uint32_t global_pointer;
  uint32_t thread_pointer;
  uint32_t temporary[3];
  uint32_t saved[2];
  uint32_t argument[8];
  uint32_t saved_high[10];
  uint32_t temporary_high[4];
};

struct an7581_trap_record {
  uint32_t valid;
  uint32_t core;
  uint32_t cause;
  uint32_t program_counter;
  uint32_t trap_value;
  uint32_t return_address;
  uint32_t stack_pointer;
  uint32_t reserved;
};

_Static_assert(sizeof(struct an7581_trap_frame) == AN7581_TRAP_FRAME_SIZE,
               "trap frame must match trap.S");
_Static_assert(offsetof(struct an7581_trap_frame, return_address) ==
                   AN7581_TRAP_FRAME_RA_OFFSET,
               "trap return-address offset must match trap.S");
_Static_assert(offsetof(struct an7581_trap_frame, stack_pointer) ==
                   AN7581_TRAP_FRAME_SP_OFFSET,
               "trap stack-pointer offset must match trap.S");
_Static_assert(sizeof(struct an7581_trap_record) == 32U,
               "trap record must remain 32 bytes");

extern volatile struct an7581_trap_record g_trap_records[AN7581_NPU_CORE_COUNT];

uint32_t an7581_trap_dispatch(uint32_t cause, uint32_t program_counter,
                              uint32_t trap_value,
                              const struct an7581_trap_frame *frame,
                              uint32_t core);

#endif

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_HARDWARE_MUTEX_H
#define AN7581_HARDWARE_MUTEX_H

#include "an7581/platform/memory_map.h"
#include "an7581/runtime/status.h"

#define AN7581_HARDWARE_MUTEX_REQUEST_BASE UINT32_C(0x1ec03180)
#define AN7581_HARDWARE_MUTEX_STATUS_BASE UINT32_C(0x1ec03000)
#define AN7581_HARDWARE_MUTEX_RELEASE_BASE UINT32_C(0x1ec03200)
#define AN7581_HARDWARE_MUTEX_HANDLE_LIMIT UINT32_C(32)
#define AN7581_HARDWARE_MUTEX_BANK_LIMIT UINT32_C(8)
#define AN7581_HARDWARE_MUTEX_ACQUIRED (UINT32_C(1) << 16)

typedef uint32_t (*an7581_hardware_mutex_hart_id_reader)(void *context);

struct an7581_hardware_mutex_bank {
  uint8_t handles[AN7581_HARDWARE_MUTEX_BANK_LIMIT];
  uint8_t fixed_hart_id;
  uint32_t handle_count;
  uint32_t held_masks[AN7581_NPU_CORE_COUNT];
  an7581_hardware_mutex_hart_id_reader read_hart_id;
  void *hart_id_context;
};

enum npu_runtime_result an7581_hardware_mutex_bank_initialize(
    struct an7581_hardware_mutex_bank *mutexes, uint32_t hart_id,
    const uint32_t *handles, uint32_t handle_count);
enum npu_runtime_result an7581_hardware_mutex_shared_bank_initialize(
    struct an7581_hardware_mutex_bank *mutexes,
    an7581_hardware_mutex_hart_id_reader read_hart_id, void *hart_id_context,
    const uint32_t *handles, uint32_t handle_count);
uint32_t an7581_hardware_mutex_read_current_hart(void *context);
enum npu_runtime_result an7581_hardware_mutex_acquire(void *context,
                                                      uint32_t lock_index);
enum npu_runtime_result an7581_hardware_mutex_release(void *context,
                                                      uint32_t lock_index);

#endif

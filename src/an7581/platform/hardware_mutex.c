/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/hardware_mutex.h"

#include "an7581/platform/mmio.h"

static bool
lock_index_is_valid(const struct an7581_hardware_mutex_bank *mutexes,
                    uint32_t lock_index) {
  return lock_index < mutexes->handle_count &&
         mutexes->handle_count <= AN7581_HARDWARE_MUTEX_BANK_LIMIT;
}

static uint32_t lock_offset(const struct an7581_hardware_mutex_bank *mutexes,
                            uint32_t lock_index) {
  return (uint32_t)mutexes->handles[lock_index] * sizeof(uint32_t);
}

static uint32_t bank_hart_id(const struct an7581_hardware_mutex_bank *mutexes) {
  if (mutexes->read_hart_id != NULL)
    return mutexes->read_hart_id(mutexes->hart_id_context);
  return mutexes->fixed_hart_id;
}

static enum npu_runtime_result bank_initialize(
    struct an7581_hardware_mutex_bank *mutexes, uint32_t fixed_hart_id,
    an7581_hardware_mutex_hart_id_reader read_hart_id, void *hart_id_context,
    const uint32_t *handles, uint32_t handle_count) {
  struct an7581_hardware_mutex_bank candidate = {0};
  uint32_t index;
  uint32_t other_index;

  if (mutexes == NULL || handles == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if ((read_hart_id == NULL && fixed_hart_id >= AN7581_NPU_CORE_COUNT) ||
      handle_count == 0U || handle_count > AN7581_HARDWARE_MUTEX_BANK_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < handle_count; ++index) {
    if (handles[index] >= AN7581_HARDWARE_MUTEX_HANDLE_LIMIT)
      return NPU_RUNTIME_OUT_OF_RANGE;
    for (other_index = 0U; other_index < index; ++other_index) {
      if (handles[index] == handles[other_index])
        return NPU_RUNTIME_OUT_OF_RANGE;
    }
    candidate.handles[index] = (uint8_t)handles[index];
  }

  candidate.fixed_hart_id = (uint8_t)fixed_hart_id;
  candidate.handle_count = handle_count;
  candidate.read_hart_id = read_hart_id;
  candidate.hart_id_context = hart_id_context;
  *mutexes = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_hardware_mutex_bank_initialize(
    struct an7581_hardware_mutex_bank *mutexes, uint32_t hart_id,
    const uint32_t *handles, uint32_t handle_count) {
  return bank_initialize(mutexes, hart_id, NULL, NULL, handles, handle_count);
}

enum npu_runtime_result an7581_hardware_mutex_shared_bank_initialize(
    struct an7581_hardware_mutex_bank *mutexes,
    an7581_hardware_mutex_hart_id_reader read_hart_id, void *hart_id_context,
    const uint32_t *handles, uint32_t handle_count) {
  if (read_hart_id == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return bank_initialize(mutexes, 0U, read_hart_id, hart_id_context, handles,
                         handle_count);
}

uint32_t an7581_hardware_mutex_read_current_hart(void *context) {
  (void)context;
#ifdef AN7581_MMIO_TEST
  return UINT32_MAX;
#else
  uint32_t hart_id;

  __asm__ volatile("csrr %0, mhartid" : "=r"(hart_id));
  return hart_id;
#endif
}

enum npu_runtime_result an7581_hardware_mutex_acquire(void *context,
                                                      uint32_t lock_index) {
  struct an7581_hardware_mutex_bank *mutexes = context;
  uint32_t hart_id;
  uint32_t lock_bit;
  uint32_t offset;
  uint32_t status;

  if (mutexes == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  hart_id = bank_hart_id(mutexes);
  if (!lock_index_is_valid(mutexes, lock_index) ||
      hart_id >= AN7581_NPU_CORE_COUNT ||
      (uint32_t)mutexes->handles[lock_index] >=
          AN7581_HARDWARE_MUTEX_HANDLE_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  lock_bit = UINT32_C(1) << lock_index;
  if ((mutexes->held_masks[hart_id] & lock_bit) != 0U)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  offset = lock_offset(mutexes, lock_index);
  an7581_mmio_write32(AN7581_HARDWARE_MUTEX_REQUEST_BASE + offset,
                      (hart_id << 8U) | UINT32_C(0x40));
  status = an7581_mmio_read32(AN7581_HARDWARE_MUTEX_STATUS_BASE +
                              (hart_id & 3U) * UINT32_C(0x400) + offset);
  if ((status & AN7581_HARDWARE_MUTEX_ACQUIRED) == 0U ||
      ((status >> 8U) & UINT32_C(0xff)) != hart_id)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  mutexes->held_masks[hart_id] |= lock_bit;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_hardware_mutex_release(void *context,
                                                      uint32_t lock_index) {
  struct an7581_hardware_mutex_bank *mutexes = context;
  uint32_t hart_id;
  uint32_t lock_bit;
  uint32_t offset;

  if (mutexes == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  hart_id = bank_hart_id(mutexes);
  if (!lock_index_is_valid(mutexes, lock_index) ||
      hart_id >= AN7581_NPU_CORE_COUNT ||
      (uint32_t)mutexes->handles[lock_index] >=
          AN7581_HARDWARE_MUTEX_HANDLE_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  lock_bit = UINT32_C(1) << lock_index;
  if ((mutexes->held_masks[hart_id] & lock_bit) == 0U)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  offset = lock_offset(mutexes, lock_index);
  an7581_mmio_write32(AN7581_HARDWARE_MUTEX_RELEASE_BASE +
                          (hart_id & 3U) * UINT32_C(0x400) + offset,
                      hart_id << 8U);
  mutexes->held_masks[hart_id] &= ~lock_bit;
  return NPU_RUNTIME_SUCCESS;
}

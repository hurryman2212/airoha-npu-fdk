/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_PLIC_H
#define AN7581_PLIC_H

#include "an7581/platform/memory_map.h"

#define AN7581_PLIC_SOURCE_COUNT 192U
#define AN7581_PLIC_PRIORITY_MASK UINT32_C(0x1f)
#define AN7581_PLIC_DEFAULT_PRIORITY 16U
#define AN7581_PLIC_RESERVED_SOURCE_PRIORITY 17U
#define AN7581_PLIC_RESERVED_SOURCE 95U

typedef void (*an7581_plic_handler)(uint32_t source, void *context);

static inline bool an7581_plic_source_is_valid(uint32_t source) {
  return source < AN7581_PLIC_SOURCE_COUNT;
}

static inline uint32_t an7581_plic_priority_address(uint32_t source) {
  return AN7581_PLIC_PRIORITY_BASE + source * sizeof(uint32_t);
}

static inline uint32_t an7581_plic_enable_set_address(uint32_t source) {
  uint32_t hardware_source = source + 1U;

  return AN7581_PLIC_ENABLE_SET_BASE +
         (hardware_source >> 5) * sizeof(uint32_t);
}

static inline uint32_t an7581_plic_enable_clear_address(uint32_t source) {
  uint32_t hardware_source = source + 1U;

  return AN7581_PLIC_ENABLE_CLEAR_BASE +
         (hardware_source >> 5) * sizeof(uint32_t);
}

static inline uint32_t an7581_plic_source_bit(uint32_t source) {
  return UINT32_C(1) << ((source + 1U) & 31U);
}

static inline bool an7581_plic_claim_to_source(uint32_t claim,
                                               uint32_t *source) {
  if (claim == 0U || claim > AN7581_PLIC_SOURCE_COUNT || source == NULL)
    return false;

  *source = claim - 1U;
  return true;
}

void an7581_plic_initialize(uint32_t hart_id);
bool an7581_plic_register_handler(uint32_t source, an7581_plic_handler handler,
                                  void *context);
bool an7581_plic_unregister_handler(uint32_t source,
                                    an7581_plic_handler handler, void *context);
bool an7581_plic_dispatch(void);
void an7581_plic_set_threshold(uint32_t threshold);
bool an7581_plic_set_priority(uint32_t source, uint32_t priority);
bool an7581_plic_set_enabled(uint32_t source, bool enabled);
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/plic.h"

#include "an7581/platform/mmio.h"

struct an7581_plic_handler_entry {
  an7581_plic_handler handler;
  void *context;
};

static struct an7581_plic_handler_entry
    g_plic_handlers[AN7581_PLIC_SOURCE_COUNT];

volatile uint32_t g_plic_spurious_interrupts;
volatile uint32_t g_plic_unhandled_interrupts;
volatile uint32_t g_plic_last_unhandled_source = UINT32_MAX;

void an7581_plic_set_threshold(uint32_t threshold) {
  an7581_mmio_write32(AN7581_PLIC_CONTEXT_BASE,
                      threshold & AN7581_PLIC_PRIORITY_MASK);
}

bool an7581_plic_set_priority(uint32_t source, uint32_t priority) {
  if (!an7581_plic_source_is_valid(source))
    return false;

  an7581_mmio_write32(an7581_plic_priority_address(source),
                      priority & AN7581_PLIC_PRIORITY_MASK);
  return true;
}

bool an7581_plic_set_enabled(uint32_t source, bool enabled) {
  uint32_t set_address;
  uint32_t clear_address;
  uint32_t source_bit;
  uint32_t value;

  if (!an7581_plic_source_is_valid(source))
    return false;

  set_address = an7581_plic_enable_set_address(source);
  clear_address = an7581_plic_enable_clear_address(source);
  source_bit = an7581_plic_source_bit(source);

  if (enabled) {
    value = an7581_mmio_read32(clear_address);
    an7581_mmio_write32(clear_address, value & ~source_bit);
    value = an7581_mmio_read32(set_address);
    an7581_mmio_write32(set_address, value | source_bit);
  } else {
    value = an7581_mmio_read32(clear_address);
    an7581_mmio_write32(clear_address, value | source_bit);
    value = an7581_mmio_read32(set_address);
    an7581_mmio_write32(set_address, value & ~source_bit);
  }

  return true;
}

void an7581_plic_initialize(uint32_t hart_id) {
  uint32_t source;

  if (hart_id >= AN7581_NPU_CORE_COUNT)
    return;

  an7581_plic_set_threshold(0U);
  for (source = 0U; source < AN7581_PLIC_SOURCE_COUNT; ++source) {
    (void)an7581_plic_set_enabled(source, false);
    (void)an7581_plic_set_priority(source,
                                   source == AN7581_PLIC_RESERVED_SOURCE
                                       ? AN7581_PLIC_RESERVED_SOURCE_PRIORITY
                                       : AN7581_PLIC_DEFAULT_PRIORITY);
    if (hart_id == 0U) {
      g_plic_handlers[source].handler = NULL;
      g_plic_handlers[source].context = NULL;
    }
  }

  if (hart_id == 0U) {
    g_plic_spurious_interrupts = 0U;
    g_plic_unhandled_interrupts = 0U;
    g_plic_last_unhandled_source = UINT32_MAX;
  }
}

bool an7581_plic_register_handler(uint32_t source, an7581_plic_handler handler,
                                  void *context) {
  if (!an7581_plic_source_is_valid(source) || handler == NULL ||
      g_plic_handlers[source].handler != NULL)
    return false;

  g_plic_handlers[source].context = context;
  an7581_memory_barrier();
  g_plic_handlers[source].handler = handler;
  an7581_memory_barrier();
  return true;
}

bool an7581_plic_unregister_handler(uint32_t source,
                                    an7581_plic_handler handler,
                                    void *context) {
  if (!an7581_plic_source_is_valid(source) || handler == NULL ||
      g_plic_handlers[source].handler != handler ||
      g_plic_handlers[source].context != context)
    return false;

  g_plic_handlers[source].handler = NULL;
  an7581_memory_barrier();
  g_plic_handlers[source].context = NULL;
  an7581_memory_barrier();
  return true;
}

bool an7581_plic_dispatch(void) {
  struct an7581_plic_handler_entry entry;
  uint32_t claim;
  uint32_t source;

  claim = an7581_mmio_read32(AN7581_PLIC_CONTEXT_BASE + sizeof(uint32_t));
  if (claim == 0U) {
    ++g_plic_spurious_interrupts;
    return true;
  }

  if (!an7581_plic_claim_to_source(claim, &source)) {
    an7581_mmio_write32(AN7581_PLIC_CONTEXT_BASE + sizeof(uint32_t), claim);
    return false;
  }

  entry = g_plic_handlers[source];
  if (entry.handler == NULL) {
    ++g_plic_unhandled_interrupts;
    g_plic_last_unhandled_source = source;
    (void)an7581_plic_set_enabled(source, false);
  } else {
    entry.handler(source, entry.context);
  }

  an7581_mmio_write32(AN7581_PLIC_CONTEXT_BASE + sizeof(uint32_t), claim);
  return true;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tr471_timer.h"

#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/plic.h"
#include "an7581/runtime/memory.h"

#define AN7581_TR471_TIMER_TICKS_PER_MHZ UINT32_C(100)

static bool timer_reload_calculate(uint32_t clock_mhz, uint32_t *reload) {
  if (clock_mhz == 0U ||
      clock_mhz > UINT32_MAX / AN7581_TR471_TIMER_TICKS_PER_MHZ ||
      reload == NULL)
    return false;

  *reload = clock_mhz * AN7581_TR471_TIMER_TICKS_PER_MHZ;
  return true;
}

enum npu_runtime_result
an7581_tr471_timer_initialize(struct an7581_tr471_timer *timer,
                              struct npu_tr471_state *state) {
  if (timer == NULL || state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (timer->initialized)
    return NPU_RUNTIME_REJECTED;

  (void)npu_memset(timer, 0U, sizeof(*timer));
  timer->tr471 = state;
  timer->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_tr471_timer_handle_interrupt(struct an7581_tr471_timer *timer,
                                    uint32_t source) {
  enum npu_runtime_result status;
  uint32_t control;

  if (timer == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!timer->initialized || timer->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (source != AN7581_TR471_TIMER_INTERRUPT_SOURCE) {
    ++timer->unexpected_source_count;
    return NPU_RUNTIME_REJECTED;
  }

  if (!timer->control_cached) {
    timer->cached_control = an7581_mmio_read32(AN7581_LOCAL_TIMER_BASE);
    timer->control_cached = true;
  }
  status = npu_tr471_timer_tick(timer->tr471);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  control = (timer->cached_control & AN7581_TR471_TIMER_CONTROL_PRESERVE_MASK) |
            AN7581_TR471_TIMER_INTERRUPT_BIT;
  an7581_mmio_write32(AN7581_LOCAL_TIMER_BASE, control);
  ++timer->interrupt_count;
  return NPU_RUNTIME_SUCCESS;
}

static void timer_interrupt_handler(uint32_t source, void *context) {
  struct an7581_tr471_timer *timer = context;

  (void)an7581_tr471_timer_handle_interrupt(timer, source);
}

enum npu_runtime_result
an7581_tr471_timer_interrupt_register(struct an7581_tr471_timer *timer,
                                      uint32_t hart_id,
                                      bool activation_allowed) {
  if (timer == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!timer->initialized || timer->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!activation_allowed || hart_id != AN7581_TR471_TIMER_HART)
    return NPU_RUNTIME_REJECTED;
  if (timer->interrupt_registered)
    return NPU_RUNTIME_REJECTED;

  if (!an7581_plic_register_handler(AN7581_TR471_TIMER_INTERRUPT_SOURCE,
                                    timer_interrupt_handler, timer) ||
      !an7581_plic_set_priority(AN7581_TR471_TIMER_INTERRUPT_SOURCE,
                                AN7581_TR471_TIMER_INTERRUPT_PRIORITY) ||
      !an7581_plic_set_enabled(AN7581_TR471_TIMER_INTERRUPT_SOURCE, true))
    return NPU_RUNTIME_OUT_OF_RANGE;

  timer->interrupt_registered = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_tr471_timer_start(struct an7581_tr471_timer *timer, uint32_t hart_id,
                         uint32_t clock_mhz, bool activation_allowed) {
  uint32_t control;
  uint32_t reload;

  if (timer == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!timer->initialized || timer->tr471 == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!activation_allowed || hart_id != AN7581_TR471_TIMER_CONTROL_HART)
    return NPU_RUNTIME_REJECTED;
  if (timer->timer_started)
    return NPU_RUNTIME_REJECTED;
  if (!timer_reload_calculate(clock_mhz, &reload))
    return NPU_RUNTIME_OUT_OF_RANGE;

  an7581_mmio_write32(
      AN7581_LOCAL_TIMER_BASE + AN7581_TR471_TIMER_RELOAD_OFFSET, reload);
  control = an7581_mmio_read32(AN7581_LOCAL_TIMER_BASE);
  an7581_mmio_write32(AN7581_LOCAL_TIMER_BASE,
                      control | AN7581_TR471_TIMER_ENABLE);
  timer->timer_started = true;
  return NPU_RUNTIME_SUCCESS;
}

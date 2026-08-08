/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/mailbox.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/plic.h"
#include "an7581/platform/timer.h"
#include "an7581/runtime/mailbox_control.h"

static bool mailbox_acknowledge_interrupt(uint32_t core) {
  uint32_t interrupt_bit = UINT32_C(1) << core;

  an7581_mmio_write32(AN7581_MBOX_INT_STATUS, interrupt_bit);
  return (an7581_mmio_read32(AN7581_MBOX_INT_STATUS) & interrupt_bit) == 0U;
}

static bool mailbox_sequence_is_new(struct an7581_mailbox_runtime *runtime,
                                    uint32_t core, uint32_t sequence) {
  uint32_t core_bit = UINT32_C(1) << core;

  if ((runtime->sequence_valid_mask & core_bit) == 0U)
    return sequence != 0U;

  return runtime->last_sequence[core] != sequence;
}

static void mailbox_record_sequence(struct an7581_mailbox_runtime *runtime,
                                    uint32_t core, uint32_t sequence) {
  runtime->last_sequence[core] = sequence;
  runtime->sequence_valid_mask |= UINT32_C(1) << core;
}

static void
mailbox_notify_dispatch_completion(struct an7581_mailbox_runtime *runtime,
                                   uint32_t core, uint32_t outer_function,
                                   bool success) {
  uint32_t response_status = 0U;

  if (!runtime->host_notifications_enabled)
    return;

  ++runtime->host_notification_requests;
  runtime->last_host_notification_result = an7581_mailbox_notify_host(
      runtime, core, outer_function, success ? UINT16_C(1) : UINT16_C(0),
      &response_status);
  if (runtime->last_host_notification_result == NPU_RUNTIME_SUCCESS)
    runtime->last_host_notification_response_status = response_status;
  else
    ++runtime->host_notification_failures;
}

static void mailbox_service(struct an7581_mailbox_runtime *runtime,
                            uint32_t core, bool from_interrupt) {
  uint32_t control_address = AN7581_MBQ0_CTRL(core, 3U);
  uint32_t control;
  uint32_t sequence;
  uint32_t interrupt_bit;
  uint32_t outer_function;
  uint32_t buffer_address;
  uint32_t buffer_size;
  uint32_t local_buffer_address;
  bool interrupt_asserted;
  bool request_applied = false;
  bool response_requested;
  bool success;

  if (runtime == NULL || runtime->firmware == NULL ||
      core >= AN7581_MBOX_INTERRUPT_CORE_COUNT)
    return;

  interrupt_bit = UINT32_C(1) << core;
  interrupt_asserted =
      (an7581_mmio_read32(AN7581_MBOX_INT_STATUS) & interrupt_bit) != 0U;
  if (from_interrupt || interrupt_asserted) {
    if (!mailbox_acknowledge_interrupt(core)) {
      ++runtime->acknowledge_failures;
      return;
    }
  }

  control = an7581_mmio_read32(control_address) & UINT32_C(0x0000ffff);
  sequence = an7581_mmio_read32(AN7581_MBQ0_CTRL(core, 2U));
  if (!npu_mailbox_request_is_pending(control)) {
    if (from_interrupt)
      ++runtime->spurious_interrupts;
    return;
  }

  response_requested = npu_mailbox_response_is_requested(control);
  if (!from_interrupt && !interrupt_asserted && !response_requested &&
      !mailbox_sequence_is_new(runtime, core, sequence)) {
    if ((runtime->sequence_valid_mask & interrupt_bit) != 0U)
      ++runtime->duplicate_requests;
    return;
  }

  if ((runtime->sequence_valid_mask & interrupt_bit) != 0U &&
      runtime->last_sequence[core] == sequence) {
    ++runtime->duplicate_requests;
    return;
  }

  mailbox_record_sequence(runtime, core, sequence);
  ++runtime->processed_requests;

  if (!response_requested) {
    an7581_mmio_write32(control_address,
                        npu_mailbox_response_control(control, true));
    an7581_memory_barrier();
  }

  buffer_address = an7581_mmio_read32(AN7581_MBQ0_CTRL(core, 0U));
  buffer_size =
      an7581_mmio_read32(AN7581_MBQ0_CTRL(core, 1U)) & UINT32_C(0x0000ffff);
  outer_function = npu_mailbox_outer_function(control);

  success = an7581_dma_buffer_map(buffer_address, buffer_size, sizeof(uint32_t),
                                  &local_buffer_address);
  if (success) {
    an7581_dma_memory_barrier();
    success = npu_mailbox_dispatch(runtime->firmware, outer_function,
                                   (void *)(uintptr_t)local_buffer_address,
                                   buffer_size, &request_applied);
    an7581_dma_memory_barrier();
    if (runtime->dispatch_observer != NULL)
      runtime->dispatch_observer(runtime->dispatch_observer_context,
                                 outer_function, request_applied);
    if (!success)
      ++runtime->dispatch_errors;
  } else {
    ++runtime->invalid_buffers;
  }

  if (response_requested)
    an7581_mmio_write32(control_address,
                        npu_mailbox_response_control(control, success));
  else
    mailbox_notify_dispatch_completion(runtime, core, outer_function, success);
}

static void mailbox_interrupt_handler(uint32_t source, void *context) {
  struct an7581_mailbox_runtime *runtime = context;
  uint32_t core;
  uint32_t hart_id;

  if (source < AN7581_MBOX_PLIC_SOURCE_BASE ||
      source >= AN7581_MBOX_PLIC_SOURCE_BASE + AN7581_MBOX_INTERRUPT_CORE_COUNT)
    return;

  core = source - AN7581_MBOX_PLIC_SOURCE_BASE;
  hart_id = an7581_hardware_mutex_read_current_hart(NULL);
  if (hart_id < AN7581_NPU_CORE_COUNT && hart_id != core)
    return;

  mailbox_service(runtime, core, true);
}

enum npu_runtime_result an7581_mailbox_runtime_reset(
    struct an7581_mailbox_runtime *runtime, struct npu_firmware_state *firmware,
    const struct an7581_mailbox_host_notification_config
        *host_notification_config) {
  struct an7581_mailbox_runtime candidate = {
      .firmware = firmware,
      .last_host_notification_result = NPU_RUNTIME_REJECTED,
  };
  const uint32_t host_notification_mutex_handle =
      AN7581_MBOX_HOST_NOTIFICATION_MUTEX_HANDLE;
  enum npu_runtime_result status;

  if (runtime == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (host_notification_config == NULL) {
    *runtime = candidate;
    return NPU_RUNTIME_SUCCESS;
  }
  if ((!host_notification_config->enabled &&
       (host_notification_config->attempts != 0U ||
        host_notification_config->timer_clock_mhz != 0U)) ||
      (host_notification_config->enabled &&
       (host_notification_config->attempts == 0U ||
        host_notification_config->timer_clock_mhz == 0U)))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!host_notification_config->enabled) {
    *runtime = candidate;
    return NPU_RUNTIME_SUCCESS;
  }

  status = an7581_hardware_mutex_shared_bank_initialize(
      &candidate.host_notification_mutexes,
      an7581_hardware_mutex_read_current_hart, NULL,
      &host_notification_mutex_handle, 1U);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  candidate.host_notification_attempts = host_notification_config->attempts;
  candidate.host_notification_timer_clock_mhz =
      host_notification_config->timer_clock_mhz;
  candidate.host_notifications_enabled = true;
  *runtime = candidate;
  return NPU_RUNTIME_SUCCESS;
}

bool an7581_mailbox_set_dispatch_observer(
    struct an7581_mailbox_runtime *runtime,
    an7581_mailbox_dispatch_observer observer, void *context) {
  if (runtime == NULL || observer == NULL)
    return false;

  runtime->dispatch_observer = observer;
  runtime->dispatch_observer_context = context;
  return true;
}

void an7581_mailbox_poll(struct an7581_mailbox_runtime *runtime,
                         uint32_t core) {
  mailbox_service(runtime, core, false);
}

bool an7581_mailbox_interrupt_initialize(struct an7581_mailbox_runtime *runtime,
                                         uint32_t core) {
  uint32_t mailbox_core;
  uint32_t source;

  if (runtime == NULL || runtime->firmware == NULL ||
      core >= AN7581_MBOX_INTERRUPT_CORE_COUNT)
    return false;

  if (core == 0U) {
    for (mailbox_core = 0U; mailbox_core < AN7581_MBOX_INTERRUPT_CORE_COUNT;
         ++mailbox_core)
      an7581_mmio_write32(AN7581_MBOX_INT_MASK(mailbox_core + 1U),
                          UINT32_C(1) << mailbox_core);
    an7581_mmio_write32(AN7581_MBOX_INT_MASK(0U), UINT32_C(1) << 8);

    for (mailbox_core = 0U;
         mailbox_core < AN7581_MBOX_INTERRUPT_CORE_COUNT - 1U; ++mailbox_core) {
      source = an7581_mailbox_source_for_core(mailbox_core);
      if (!an7581_plic_register_handler(source, mailbox_interrupt_handler,
                                        runtime) ||
          !an7581_plic_set_enabled(source, true))
        return false;
    }

    return true;
  }

  source = an7581_mailbox_source_for_core(core);
  if (!an7581_plic_register_handler(source, mailbox_interrupt_handler, runtime))
    return false;

  return an7581_plic_set_enabled(source, true);
}

enum npu_runtime_result
an7581_mailbox_notify_host(struct an7581_mailbox_runtime *runtime,
                           uint32_t channel, uint32_t outer_function,
                           uint16_t payload, uint32_t *response_status) {
  enum npu_runtime_result release_status;
  enum npu_runtime_result status;
  uint32_t control;
  uint32_t sequence;
  uint32_t attempt;

  if (runtime == NULL || channel > UINT32_C(0x0f) ||
      outer_function > UINT32_C(0x0f) || response_status == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!runtime->host_notifications_enabled ||
      runtime->host_notification_timer_clock_mhz == 0U)
    return NPU_RUNTIME_REJECTED;

  status =
      an7581_hardware_mutex_acquire(&runtime->host_notification_mutexes, 0U);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  an7581_mmio_write32(AN7581_MBQ8_CTRL(0U, 0U), channel);
  an7581_mmio_write32(AN7581_MBQ8_CTRL(0U, 1U), payload);
  an7581_mmio_write32(AN7581_MBQ8_CTRL(0U, 3U),
                      outer_function << NPU_MBOX_CONTROL_FUNCTION_SHIFT);
  sequence = an7581_mmio_read32(AN7581_MBQ8_CTRL(0U, 2U));
  an7581_mmio_write32(AN7581_MBQ8_CTRL(0U, 2U), sequence + 1U);

  status = NPU_RUNTIME_TIMEOUT;
  for (attempt = 0U; attempt < runtime->host_notification_attempts; ++attempt) {
    if (!an7581_local_timer_delay_ms(
            1U, runtime->host_notification_timer_clock_mhz)) {
      status = NPU_RUNTIME_IO_ERROR;
      break;
    }

    control =
        an7581_mmio_read32(AN7581_MBQ8_CTRL(0U, 3U)) & UINT32_C(0x0000ffff);
    if ((control & NPU_MBOX_CONTROL_DONE) != 0U) {
      *response_status = npu_mailbox_response_status(control);
      status = NPU_RUNTIME_SUCCESS;
      break;
    }
  }

  release_status =
      an7581_hardware_mutex_release(&runtime->host_notification_mutexes, 0U);
  if (status == NPU_RUNTIME_SUCCESS)
    return release_status;
  return status;
}

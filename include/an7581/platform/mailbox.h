/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_PLATFORM_MAILBOX_H
#define AN7581_PLATFORM_MAILBOX_H

#include "an7581/platform/hardware_mutex.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/mailbox.h"

#define AN7581_MBOX_PLIC_SOURCE_BASE 8U
#define AN7581_MBOX_INTERRUPT_CORE_COUNT AN7581_NPU_CORE_COUNT
#define AN7581_MBOX_HOST_NOTIFICATION_MUTEX_HANDLE UINT32_C(0x1e)

typedef void (*an7581_mailbox_dispatch_observer)(void *context,
                                                 uint32_t outer_function,
                                                 bool request_applied);

struct an7581_mailbox_host_notification_config {
  uint32_t attempts;
  uint32_t timer_clock_mhz;
  bool enabled;
};

struct an7581_mailbox_runtime {
  struct npu_firmware_state *firmware;
  an7581_mailbox_dispatch_observer dispatch_observer;
  void *dispatch_observer_context;
  struct an7581_hardware_mutex_bank host_notification_mutexes;
  uint32_t last_sequence[AN7581_MBOX_INTERRUPT_CORE_COUNT];
  uint32_t sequence_valid_mask;
  uint32_t host_notification_attempts;
  uint32_t host_notification_timer_clock_mhz;
  uint32_t host_notification_requests;
  uint32_t host_notification_failures;
  uint32_t last_host_notification_response_status;
  uint32_t processed_requests;
  uint32_t duplicate_requests;
  uint32_t spurious_interrupts;
  uint32_t acknowledge_failures;
  uint32_t invalid_buffers;
  uint32_t dispatch_errors;
  enum npu_runtime_result last_host_notification_result;
  bool host_notifications_enabled;
};

static inline uint32_t an7581_mailbox_source_for_core(uint32_t core) {
  return AN7581_MBOX_PLIC_SOURCE_BASE + core;
}

enum npu_runtime_result an7581_mailbox_runtime_reset(
    struct an7581_mailbox_runtime *runtime, struct npu_firmware_state *firmware,
    const struct an7581_mailbox_host_notification_config
        *host_notification_config);
bool an7581_mailbox_set_dispatch_observer(
    struct an7581_mailbox_runtime *runtime,
    an7581_mailbox_dispatch_observer observer, void *context);
void an7581_mailbox_poll(struct an7581_mailbox_runtime *runtime, uint32_t core);
bool an7581_mailbox_interrupt_initialize(struct an7581_mailbox_runtime *runtime,
                                         uint32_t core);
enum npu_runtime_result
an7581_mailbox_notify_host(struct an7581_mailbox_runtime *runtime,
                           uint32_t channel, uint32_t outer_function,
                           uint16_t payload, uint32_t *response_status);

#endif

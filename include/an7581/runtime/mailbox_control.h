/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_MAILBOX_CONTROL_H
#define NPU_MAILBOX_CONTROL_H

#include "an7581/platform/types.h"

#define NPU_MBOX_CONTROL_FUNCTION_SHIFT 11U
#define NPU_MBOX_CONTROL_FUNCTION_MASK UINT32_C(0x00007800)
#define NPU_MBOX_CONTROL_STATUS_MASK UINT32_C(0x0000001c)
#define NPU_MBOX_CONTROL_STATUS_SHIFT 2U
#define NPU_MBOX_CONTROL_DONE UINT32_C(0x00000002)
#define NPU_MBOX_CONTROL_WAIT_RESPONSE UINT32_C(0x00000001)

static inline bool npu_mailbox_request_is_pending(uint32_t control) {
  return (control & NPU_MBOX_CONTROL_DONE) == 0U;
}

static inline bool npu_mailbox_response_is_requested(uint32_t control) {
  return (control & NPU_MBOX_CONTROL_WAIT_RESPONSE) != 0U;
}

static inline uint32_t npu_mailbox_outer_function(uint32_t control) {
  return (control & NPU_MBOX_CONTROL_FUNCTION_MASK) >>
         NPU_MBOX_CONTROL_FUNCTION_SHIFT;
}

static inline uint32_t npu_mailbox_response_status(uint32_t control) {
  return (control & NPU_MBOX_CONTROL_STATUS_MASK) >>
         NPU_MBOX_CONTROL_STATUS_SHIFT;
}

static inline uint32_t npu_mailbox_response_control(uint32_t request,
                                                    bool success) {
  uint32_t status = success ? 1U : 0U;

  return (request & ~(NPU_MBOX_CONTROL_STATUS_MASK | NPU_MBOX_CONTROL_DONE)) |
         (status << NPU_MBOX_CONTROL_STATUS_SHIFT) | NPU_MBOX_CONTROL_DONE;
}

#endif

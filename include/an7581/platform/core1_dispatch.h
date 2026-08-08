/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_CORE1_DISPATCH_H
#define AN7581_CORE1_DISPATCH_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define AN7581_CORE1_HART UINT32_C(1)
#define AN7581_CORE1_HART_MASK (UINT32_C(1) << AN7581_CORE1_HART)

enum an7581_core1_dispatch_role {
  AN7581_CORE1_DISPATCH_ROLE_NONE = 0,
  AN7581_CORE1_DISPATCH_ROLE_RX_REFILL,
};

struct an7581_core1_worker_result {
  bool should_backoff;
};

typedef enum npu_runtime_result (*an7581_core1_worker_step)(
    void *context, struct an7581_core1_worker_result *result);

struct an7581_core1_dispatch {
  an7581_core1_worker_step volatile worker;
  void *volatile worker_context;
  volatile bool quiesce_requested;
  volatile bool quiesced;
  bool initialized;
};

struct an7581_core1_dispatch_result {
  enum an7581_core1_dispatch_role role;
  enum npu_runtime_result status;
  bool waiting_for_worker;
  bool quiesce_requested;
  bool quiesced;
  bool should_backoff;
};

enum npu_runtime_result
an7581_core1_dispatch_initialize(struct an7581_core1_dispatch *dispatch);
enum npu_runtime_result
an7581_core1_dispatch_publish(struct an7581_core1_dispatch *dispatch,
                              an7581_core1_worker_step worker,
                              void *worker_context);
enum npu_runtime_result
an7581_core1_dispatch_request_quiesce(struct an7581_core1_dispatch *dispatch,
                                      void *worker_context);
enum npu_runtime_result
an7581_core1_dispatch_unpublish(struct an7581_core1_dispatch *dispatch,
                                void *worker_context);
enum npu_runtime_result
an7581_core1_dispatch_resume(struct an7581_core1_dispatch *dispatch);
enum npu_runtime_result
an7581_core1_dispatch_step(struct an7581_core1_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core1_dispatch_result *result);

#endif

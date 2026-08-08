/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_CORE4_DISPATCH_H
#define AN7581_CORE4_DISPATCH_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define AN7581_CORE4_HART UINT32_C(4)

enum an7581_core4_dispatch_role {
  AN7581_CORE4_DISPATCH_ROLE_NONE = 0,
  AN7581_CORE4_DISPATCH_ROLE_TX_DONE,
};

struct an7581_core4_worker_result {
  bool should_backoff;
};

typedef enum npu_runtime_result (*an7581_core4_worker_step)(
    void *context, struct an7581_core4_worker_result *result);

struct an7581_core4_dispatch {
  an7581_core4_worker_step volatile worker;
  void *volatile worker_context;
  bool initialized;
};

struct an7581_core4_dispatch_result {
  enum an7581_core4_dispatch_role role;
  enum npu_runtime_result status;
  bool waiting_for_worker;
  bool should_backoff;
};

enum npu_runtime_result
an7581_core4_dispatch_step(struct an7581_core4_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core4_dispatch_result *result);

#endif

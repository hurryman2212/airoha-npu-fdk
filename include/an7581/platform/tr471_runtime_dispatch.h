/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_RUNTIME_DISPATCH_H
#define AN7581_TR471_RUNTIME_DISPATCH_H

#include "an7581/platform/tr471_timer.h"
#include "an7581/services/tr471/runtime.h"

#define AN7581_TR471_CONTROL_HART_MASK                                         \
  (UINT32_C(1) << AN7581_TR471_TIMER_CONTROL_HART)
#define AN7581_TR471_TIMER_HART_MASK (UINT32_C(1) << AN7581_TR471_TIMER_HART)
#define AN7581_TR471_RUNTIME_HART_MASK                                         \
  (UINT32_C(1) << AN7581_TR471_RUNTIME_HART)

enum an7581_tr471_runtime_dispatch_role {
  AN7581_TR471_RUNTIME_DISPATCH_ROLE_NONE = 0,
  AN7581_TR471_RUNTIME_DISPATCH_ROLE_TIMER,
  AN7581_TR471_RUNTIME_DISPATCH_ROLE_RUNTIME,
};

typedef enum npu_runtime_result (*an7581_tr471_runtime_dispatch_wake)(
    void *context, uint32_t hart_mask);

struct an7581_tr471_runtime_dispatch_config {
  struct npu_tr471_runtime_io *runtime;
  struct an7581_tr471_timer *timer;
  an7581_tr471_runtime_dispatch_wake wake_control;
  void *wake_context;
  uint32_t transmit_budget;
  uint32_t receive_budget;
};

struct an7581_tr471_runtime_dispatch {
  struct npu_tr471_runtime_io *volatile runtime;
  struct an7581_tr471_timer *volatile timer;
  an7581_tr471_runtime_dispatch_wake volatile wake_control;
  void *volatile wake_context;
  uint32_t transmit_budget;
  uint32_t receive_budget;
  volatile bool control_notified;
  volatile bool timer_worker_ready;
  volatile bool published;
};

struct an7581_tr471_runtime_dispatch_result {
  struct npu_tr471_runtime_step_result service;
  enum an7581_tr471_runtime_dispatch_role role;
  enum npu_runtime_result status;
  bool waiting_for_publication;
  bool waiting_for_timer;
  bool timer_worker_ready;
  bool should_backoff;
};

enum npu_runtime_result an7581_tr471_runtime_dispatch_publish(
    struct an7581_tr471_runtime_dispatch *dispatch,
    const struct an7581_tr471_runtime_dispatch_config *config);
bool an7581_tr471_runtime_dispatch_timer_worker_ready(
    const struct an7581_tr471_runtime_dispatch *dispatch);
enum npu_runtime_result an7581_tr471_runtime_dispatch_step(
    struct an7581_tr471_runtime_dispatch *dispatch, uint32_t hart_id,
    struct an7581_tr471_runtime_dispatch_result *result);

#endif

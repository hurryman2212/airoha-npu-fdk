/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_RUNTIME_LIFECYCLE_H
#define AN7581_TR471_RUNTIME_LIFECYCLE_H

#include "an7581/platform/tr471_lifecycle.h"
#include "an7581/platform/tr471_runtime_dispatch.h"

enum an7581_tr471_runtime_lifecycle_state {
  AN7581_TR471_RUNTIME_LIFECYCLE_UNINITIALIZED = 0,
  AN7581_TR471_RUNTIME_LIFECYCLE_ACTIVATION_GATED,
  AN7581_TR471_RUNTIME_LIFECYCLE_STARTING_SERVICE,
  AN7581_TR471_RUNTIME_LIFECYCLE_INITIALIZING_TIMER,
  AN7581_TR471_RUNTIME_LIFECYCLE_PUBLISHING_WORKERS,
  AN7581_TR471_RUNTIME_LIFECYCLE_WAKING_TIMER_WORKER,
  AN7581_TR471_RUNTIME_LIFECYCLE_WAITING_FOR_TIMER_WORKER,
  AN7581_TR471_RUNTIME_LIFECYCLE_STARTING_TIMER,
  AN7581_TR471_RUNTIME_LIFECYCLE_WAKING_RUNTIME_WORKER,
  AN7581_TR471_RUNTIME_LIFECYCLE_RETRYABLE_FAILURE,
  AN7581_TR471_RUNTIME_LIFECYCLE_ACTIVE,
};

typedef enum npu_runtime_result (*an7581_tr471_runtime_service_step_operation)(
    void *context);
typedef enum npu_runtime_result (
    *an7581_tr471_runtime_initialize_timer_operation)(void *context);
typedef enum npu_runtime_result (
    *an7581_tr471_runtime_publish_workers_operation)(void *context);
typedef enum npu_runtime_result (*an7581_tr471_runtime_wake_harts_operation)(
    void *context, uint32_t hart_mask);
typedef bool (*an7581_tr471_runtime_timer_worker_ready_operation)(
    void *context);
typedef enum npu_runtime_result (*an7581_tr471_runtime_start_timer_operation)(
    void *context);

struct an7581_tr471_runtime_lifecycle_operations {
  an7581_tr471_runtime_service_step_operation step_service;
  an7581_tr471_runtime_initialize_timer_operation initialize_timer;
  an7581_tr471_runtime_publish_workers_operation publish_workers;
  an7581_tr471_runtime_wake_harts_operation wake_harts;
  an7581_tr471_runtime_timer_worker_ready_operation timer_worker_ready;
  an7581_tr471_runtime_start_timer_operation start_timer;
};

struct an7581_tr471_runtime_lifecycle_config {
  const struct an7581_tr471_runtime_lifecycle_operations *operations;
  void *operation_context;
  bool activation_allowed;
};

struct an7581_tr471_runtime_lifecycle {
  const struct an7581_tr471_runtime_lifecycle_operations *operations;
  void *operation_context;
  enum an7581_tr471_runtime_lifecycle_state state;
  enum npu_runtime_result last_status;
  uint32_t step_count;
  uint32_t service_attempt_count;
  uint32_t timer_initialize_attempt_count;
  uint32_t publication_attempt_count;
  uint32_t timer_wake_attempt_count;
  uint32_t timer_wait_count;
  uint32_t timer_start_attempt_count;
  uint32_t runtime_wake_attempt_count;
  uint32_t retryable_failure_count;
  uint32_t activation_gate_count;
  bool activation_allowed;
  bool service_active;
  bool timer_initialized;
  bool workers_published;
  bool timer_worker_woken;
  bool timer_started;
  bool runtime_worker_woken;
  bool initialized;
};

struct an7581_tr471_runtime_lifecycle_result {
  enum an7581_tr471_runtime_lifecycle_state state;
  enum npu_runtime_result status;
  bool activation_gated;
  bool waiting_for_timer_worker;
  bool service_active;
  bool timer_initialized;
  bool workers_published;
  bool timer_worker_woken;
  bool timer_started;
  bool runtime_worker_woken;
  bool active;
};

struct an7581_tr471_runtime_platform_config {
  struct an7581_tr471_lifecycle *service_lifecycle;
  struct npu_tr471_state *state;
  struct npu_tr471_runtime_io *runtime;
  struct an7581_tr471_runtime_dispatch *dispatch;
  an7581_tr471_runtime_dispatch_wake wake_harts;
  void *wake_context;
  uint32_t timer_clock_mhz;
  uint32_t transmit_budget;
  uint32_t receive_budget;
};

struct an7581_tr471_runtime_platform {
  struct an7581_tr471_lifecycle *service_lifecycle;
  struct npu_tr471_state *state;
  struct npu_tr471_runtime_io *runtime;
  struct an7581_tr471_runtime_dispatch *dispatch;
  an7581_tr471_runtime_dispatch_wake wake_harts;
  void *wake_context;
  struct an7581_tr471_timer timer;
  uint32_t timer_clock_mhz;
  uint32_t transmit_budget;
  uint32_t receive_budget;
  bool initialized;
};

enum npu_runtime_result an7581_tr471_runtime_lifecycle_initialize(
    struct an7581_tr471_runtime_lifecycle *lifecycle,
    const struct an7581_tr471_runtime_lifecycle_config *config);
enum npu_runtime_result an7581_tr471_runtime_lifecycle_step(
    struct an7581_tr471_runtime_lifecycle *lifecycle,
    struct an7581_tr471_runtime_lifecycle_result *result);
enum npu_runtime_result an7581_tr471_runtime_platform_initialize(
    struct an7581_tr471_runtime_platform *platform,
    const struct an7581_tr471_runtime_platform_config *config);
const struct an7581_tr471_runtime_lifecycle_operations *
an7581_tr471_runtime_platform_operations(void);

#endif

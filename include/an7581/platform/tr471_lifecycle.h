/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_LIFECYCLE_H
#define AN7581_TR471_LIFECYCLE_H

#include "an7581/platform/tr471_tdma.h"
#include "an7581/platform/tr471_timer.h"
#include "an7581/services/tr471/runtime.h"

#define AN7581_TR471_REQUIRED_BUFFER_ADDRESS (UINT32_C(1) << 0)
#define AN7581_TR471_REQUIRED_BUFFER_EXTENT (UINT32_C(1) << 1)
#define AN7581_TR471_REQUIRED_FLOW (UINT32_C(1) << 2)
#define AN7581_TR471_REQUIRED_ALL                                              \
  (AN7581_TR471_REQUIRED_BUFFER_ADDRESS |                                      \
   AN7581_TR471_REQUIRED_BUFFER_EXTENT | AN7581_TR471_REQUIRED_FLOW)

enum an7581_tr471_lifecycle_state {
  AN7581_TR471_LIFECYCLE_UNINITIALIZED = 0,
  AN7581_TR471_LIFECYCLE_WAITING_FOR_CONFIGURATION,
  AN7581_TR471_LIFECYCLE_ACTIVATION_GATED,
  AN7581_TR471_LIFECYCLE_INITIALIZING_TDMA,
  AN7581_TR471_LIFECYCLE_INITIALIZING_RUNTIME,
  AN7581_TR471_LIFECYCLE_RECONFIGURING_FLOW,
  AN7581_TR471_LIFECYCLE_CONFIGURATION_CONFLICT,
  AN7581_TR471_LIFECYCLE_RETRYABLE_FAILURE,
  AN7581_TR471_LIFECYCLE_ACTIVE,
};

struct an7581_tr471_configuration_readiness {
  uint32_t missing;
  uint32_t invalid;
};

typedef enum npu_runtime_result (*an7581_tr471_initialize_tdma_operation)(
    void *context, const struct npu_tr471_state *state,
    uint32_t shared_buffer_extent);
typedef enum npu_runtime_result (*an7581_tr471_initialize_runtime_operation)(
    void *context, struct npu_tr471_state *state);
typedef enum npu_runtime_result (*an7581_tr471_reset_flow_operation)(
    void *context);
typedef enum npu_runtime_result (
    *an7581_tr471_configuration_readiness_operation)(
    void *context, const struct npu_tr471_state *state,
    uint32_t shared_buffer_extent,
    struct an7581_tr471_configuration_readiness *readiness);
typedef uint32_t (*an7581_tr471_buffer_revision_operation)(
    void *context, const struct npu_tr471_state *state);

struct an7581_tr471_lifecycle_operations {
  an7581_tr471_initialize_tdma_operation initialize_tdma;
  an7581_tr471_initialize_runtime_operation initialize_runtime;
  an7581_tr471_reset_flow_operation reset_flow;
};

struct an7581_tr471_lifecycle_config {
  struct npu_tr471_state *state;
  const struct an7581_tr471_lifecycle_operations *operations;
  an7581_tr471_configuration_readiness_operation configuration_readiness;
  an7581_tr471_buffer_revision_operation buffer_revision;
  void *operation_context;
  uint32_t shared_buffer_extent;
  bool activation_allowed;
};

struct an7581_tr471_lifecycle {
  struct npu_tr471_state *tr471;
  const struct an7581_tr471_lifecycle_operations *operations;
  an7581_tr471_configuration_readiness_operation configuration_readiness;
  an7581_tr471_buffer_revision_operation buffer_revision;
  void *operation_context;
  enum an7581_tr471_lifecycle_state state;
  enum npu_runtime_result last_status;
  uint32_t shared_buffer_extent;
  uint32_t observed_flow_revision;
  uint32_t observed_buffer_revision;
  uint32_t step_count;
  uint32_t configuration_wait_count;
  uint32_t activation_gate_count;
  uint32_t tdma_attempt_count;
  uint32_t runtime_attempt_count;
  uint32_t flow_reset_attempt_count;
  uint32_t retryable_failure_count;
  bool activation_allowed;
  bool tdma_initialized;
  bool runtime_initialized;
  bool initialized;
};

struct an7581_tr471_lifecycle_result {
  struct an7581_tr471_configuration_readiness readiness;
  enum an7581_tr471_lifecycle_state state;
  enum npu_runtime_result status;
  bool waiting_for_configuration;
  bool activation_gated;
  bool configuration_conflict;
  bool tdma_initialized;
  bool runtime_initialized;
  bool flow_reconfigured;
  bool active;
};

struct an7581_tr471_lifecycle_platform {
  struct an7581_tr471_tdma_platform tdma;
  struct npu_tr471_runtime_io runtime;
  bool initialized;
};

enum npu_runtime_result an7581_tr471_configuration_readiness(
    const struct npu_tr471_state *state, uint32_t shared_buffer_extent,
    struct an7581_tr471_configuration_readiness *readiness);
enum npu_runtime_result an7581_tr471_lifecycle_initialize(
    struct an7581_tr471_lifecycle *lifecycle,
    const struct an7581_tr471_lifecycle_config *config);
enum npu_runtime_result
an7581_tr471_lifecycle_step(struct an7581_tr471_lifecycle *lifecycle,
                            struct an7581_tr471_lifecycle_result *result);
enum npu_runtime_result an7581_tr471_lifecycle_platform_initialize(
    struct an7581_tr471_lifecycle_platform *platform);
const struct an7581_tr471_lifecycle_operations *
an7581_tr471_lifecycle_platform_operations(void);

#endif

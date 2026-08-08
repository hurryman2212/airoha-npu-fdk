/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_H
#define AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_H

#include "an7581/platform/core2_dispatch.h"
#include "an7581/platform/wifi_mt7996_completion_runtime.h"
#include "an7581/platform/wifi_tx_fast_path.h"

#define AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_PACKET_BUFFER                 \
  (UINT32_C(1) << 0)
#define AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_STATE (UINT32_C(1) << 1)
#define AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_COUNT (UINT32_C(1) << 2)
#define AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_BAND0 (UINT32_C(1) << 3)
#define AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_BAND2 (UINT32_C(1) << 4)
#define AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_ALL                           \
  (AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_PACKET_BUFFER |                    \
   AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_STATE |                      \
   AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_TOKEN_COUNT |                      \
   AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_BAND0 |                            \
   AN7581_WIFI_MT7996_TX_FAST_PATH_REQUIRED_BAND2)

enum an7581_wifi_mt7996_tx_fast_path_lifecycle_state {
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_UNINITIALIZED = 0,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAITING_FOR_CONFIGURATION,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_ACTIVATION_GATED,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAITING_FOR_SHARED_STATE,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_INITIALIZING,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_PUBLISHING,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAKING_WORKER,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_RETRYABLE_FAILURE,
  AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_ACTIVE,
};

struct an7581_wifi_mt7996_tx_fast_path_configuration_readiness {
  uint32_t missing;
  uint32_t invalid;
};

typedef enum npu_runtime_result (
    *an7581_wifi_mt7996_tx_fast_path_lifecycle_configuration_operation)(
    void *context, const struct npu_wifi_configuration *configuration);
typedef enum npu_runtime_result (
    *an7581_wifi_mt7996_tx_fast_path_lifecycle_operation)(void *context);
typedef enum npu_runtime_result (
    *an7581_wifi_mt7996_tx_fast_path_lifecycle_wake_operation)(
    void *context, uint32_t hart_mask);

struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations {
  an7581_wifi_mt7996_tx_fast_path_lifecycle_configuration_operation initialize;
  an7581_wifi_mt7996_tx_fast_path_lifecycle_operation publish;
  an7581_wifi_mt7996_tx_fast_path_lifecycle_wake_operation wake_worker;
};

struct an7581_wifi_mt7996_tx_fast_path_lifecycle_config {
  const struct npu_wifi_configuration *configuration;
  const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations *operations;
  void *operation_context;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_tx_fast_path_lifecycle {
  const struct npu_wifi_configuration *configuration;
  const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations *operations;
  void *operation_context;
  enum an7581_wifi_mt7996_tx_fast_path_lifecycle_state state;
  enum npu_runtime_result last_status;
  uint32_t step_count;
  uint32_t configuration_wait_count;
  uint32_t activation_gate_count;
  uint32_t shared_state_wait_count;
  uint32_t initialization_attempt_count;
  uint32_t publication_attempt_count;
  uint32_t wake_attempt_count;
  uint32_t retryable_failure_count;
  bool activation_allowed;
  bool fast_path_initialized;
  bool fast_path_published;
  bool worker_woken;
  bool initialized;
};

struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result {
  struct an7581_wifi_mt7996_tx_fast_path_configuration_readiness readiness;
  enum an7581_wifi_mt7996_tx_fast_path_lifecycle_state state;
  enum npu_runtime_result status;
  bool waiting_for_configuration;
  bool activation_gated;
  bool waiting_for_shared_state;
  bool fast_path_initialized;
  bool fast_path_published;
  bool worker_woken;
  bool active;
};

typedef enum npu_runtime_result (*an7581_wifi_mt7996_tx_fast_path_worker_wake)(
    void *context, uint32_t hart_mask);

struct an7581_wifi_mt7996_tx_fast_path_platform_config {
  struct an7581_core2_dispatch *dispatch;
  struct npu_wifi_sram_allocator *shared_allocator;
  struct npu_wifi_packet_id_pool *shared_packet_pool;
  struct an7581_wifi_mt7996_runtime_readiness_state *readiness;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  an7581_wifi_mt7996_tx_fast_path_worker_wake wake_worker;
  void *wake_context;
  uint32_t vdma_poll_limit;
};

struct an7581_wifi_mt7996_tx_fast_path_platform {
  struct an7581_wifi_tx_fast_path_platform fast_path;
  struct an7581_core2_dispatch *dispatch;
  struct npu_wifi_sram_allocator *shared_allocator;
  struct npu_wifi_packet_id_pool *shared_packet_pool;
  struct an7581_wifi_mt7996_runtime_readiness_state *readiness;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  an7581_wifi_mt7996_tx_fast_path_worker_wake wake_worker;
  void *wake_context;
  uint32_t vdma_poll_limit;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_configuration_readiness(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_tx_fast_path_configuration_readiness *readiness);
enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_lifecycle_initialize(
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_config *config);
enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_lifecycle_step(
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result *result);
enum npu_runtime_result an7581_wifi_mt7996_tx_fast_path_platform_initialize(
    struct an7581_wifi_mt7996_tx_fast_path_platform *platform,
    const struct an7581_wifi_mt7996_tx_fast_path_platform_config *config);
const struct an7581_wifi_mt7996_tx_fast_path_lifecycle_operations *
an7581_wifi_mt7996_tx_fast_path_platform_operations(void);

#endif

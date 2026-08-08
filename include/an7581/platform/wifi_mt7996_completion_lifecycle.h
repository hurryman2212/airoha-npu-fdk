/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_H
#define AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_H

#include "an7581/platform/hardware_mutex.h"
#include "an7581/platform/wifi_mt7996_completion_dispatch.h"
#include "an7581/services/wifi/region.h"

#define AN7581_WIFI_MT7996_COMPLETION_REQUIRED_PACKET_BUFFER (UINT32_C(1) << 0)
#define AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TOKEN_ID_SIZE (UINT32_C(1) << 1)
#define AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_RING (UINT32_C(1) << 2)
#define AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_REGISTERS               \
  (UINT32_C(1) << 3)
#define AN7581_WIFI_MT7996_COMPLETION_REQUIRED_ALL                             \
  (AN7581_WIFI_MT7996_COMPLETION_REQUIRED_PACKET_BUFFER |                      \
   AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TOKEN_ID_SIZE |                      \
   AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_RING |                       \
   AN7581_WIFI_MT7996_COMPLETION_REQUIRED_TX_DONE_REGISTERS)
#define AN7581_WIFI_MT7996_COMPLETION_WORKER_HART_MASK                         \
  ((UINT32_C(1) << AN7581_WIFI_MT7996_COMPLETION_PACKET_QUEUE_HART) |          \
   (UINT32_C(1) << AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART))

enum an7581_wifi_mt7996_completion_lifecycle_state {
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_UNINITIALIZED = 0,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_WAITING_FOR_CONFIGURATION,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVATION_GATED,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_INITIALIZING_SHARED_STATE,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_INITIALIZING_PIPELINE,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_PUBLISHING,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_WAKING_WORKERS,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_RETRYABLE_FAILURE,
  AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVE,
};

struct an7581_wifi_mt7996_completion_configuration_readiness {
  uint32_t missing;
  uint32_t invalid;
};

typedef enum npu_runtime_result (
    *an7581_wifi_mt7996_completion_lifecycle_configuration_operation)(
    void *context, const struct npu_wifi_configuration *configuration);
typedef enum npu_runtime_result (
    *an7581_wifi_mt7996_completion_lifecycle_operation)(void *context);
typedef enum npu_runtime_result (
    *an7581_wifi_mt7996_completion_lifecycle_wake_operation)(
    void *context, uint32_t hart_mask);

struct an7581_wifi_mt7996_completion_lifecycle_operations {
  an7581_wifi_mt7996_completion_lifecycle_configuration_operation
      initialize_shared_state;
  an7581_wifi_mt7996_completion_lifecycle_configuration_operation
      initialize_pipeline;
  an7581_wifi_mt7996_completion_lifecycle_operation publish_pipeline;
  an7581_wifi_mt7996_completion_lifecycle_wake_operation wake_workers;
};

struct an7581_wifi_mt7996_completion_lifecycle_config {
  const struct npu_wifi_configuration *configuration;
  const struct an7581_wifi_mt7996_completion_lifecycle_operations *operations;
  void *operation_context;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_completion_lifecycle {
  const struct npu_wifi_configuration *configuration;
  const struct an7581_wifi_mt7996_completion_lifecycle_operations *operations;
  void *operation_context;
  enum an7581_wifi_mt7996_completion_lifecycle_state state;
  enum npu_runtime_result last_status;
  uint32_t step_count;
  uint32_t configuration_wait_count;
  uint32_t activation_gate_count;
  uint32_t shared_state_attempt_count;
  uint32_t pipeline_attempt_count;
  uint32_t publication_attempt_count;
  uint32_t wake_attempt_count;
  uint32_t retryable_failure_count;
  bool activation_allowed;
  bool shared_state_initialized;
  bool pipeline_initialized;
  bool pipeline_published;
  bool workers_woken;
  bool initialized;
};

struct an7581_wifi_mt7996_completion_lifecycle_result {
  struct an7581_wifi_mt7996_completion_configuration_readiness readiness;
  enum an7581_wifi_mt7996_completion_lifecycle_state state;
  enum npu_runtime_result status;
  bool waiting_for_configuration;
  bool activation_gated;
  bool shared_state_initialized;
  bool pipeline_initialized;
  bool pipeline_published;
  bool workers_woken;
  bool active;
};

struct an7581_wifi_mt7996_completion_shared_memory {
  volatile uint16_t *token_entries;
  volatile uint16_t *packet_recycle_entries;
};

typedef enum npu_runtime_result (*an7581_wifi_mt7996_completion_worker_wake)(
    void *context, uint32_t hart_mask);

struct an7581_wifi_mt7996_completion_platform_config {
  struct an7581_wifi_mt7996_completion_dispatch *dispatch;
  struct an7581_wifi_mt7996_runtime_readiness_state *readiness;
  struct npu_wifi_sram_allocator *shared_allocator;
  struct npu_wifi_packet_id_pool *shared_packet_pool;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  an7581_wifi_mt7996_completion_worker_wake wake_workers;
  void *wake_context;
  an7581_hardware_mutex_hart_id_reader read_hart_id;
  void *hart_id_context;
  uint32_t vdma_poll_limit;
  uint32_t tx_done_budget;
  uint32_t band0_budget;
  uint16_t packet_queue_producer;
  uint16_t packet_queue_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
};

struct an7581_wifi_mt7996_completion_platform {
  struct npu_wifi_sram_allocator allocator;
  struct an7581_hardware_mutex_bank packet_pool_mutexes;
  struct npu_wifi_packet_id_pool packet_pool;
  struct npu_wifi_sram_allocator *allocator_owner;
  struct npu_wifi_packet_id_pool *packet_pool_owner;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  struct an7581_wifi_mt7996_completion_pipeline pipeline;
  struct an7581_wifi_mt7996_completion_dispatch *dispatch;
  struct an7581_wifi_mt7996_runtime_readiness_state *readiness;
  an7581_wifi_mt7996_completion_worker_wake wake_workers;
  void *wake_context;
  an7581_hardware_mutex_hart_id_reader read_hart_id;
  void *hart_id_context;
  uint32_t vdma_poll_limit;
  uint32_t tx_done_budget;
  uint32_t band0_budget;
  uint16_t packet_queue_producer;
  uint16_t packet_queue_consumers[NPU_WIFI_MT7996_COMPLETION_BAND_COUNT];
  bool shared_state_external;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_completion_configuration_readiness(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_completion_configuration_readiness *readiness);
enum npu_runtime_result an7581_wifi_mt7996_completion_lifecycle_initialize(
    struct an7581_wifi_mt7996_completion_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_completion_lifecycle_config *config);
enum npu_runtime_result an7581_wifi_mt7996_completion_lifecycle_step(
    struct an7581_wifi_mt7996_completion_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_completion_lifecycle_result *result);

enum npu_runtime_result an7581_wifi_mt7996_completion_shared_memory_resolve(
    struct npu_wifi_sram_allocator *allocator,
    struct an7581_wifi_mt7996_completion_shared_memory *memory);
enum npu_runtime_result
an7581_wifi_mt7996_completion_platform_shared_state_initialize(
    struct an7581_wifi_mt7996_completion_platform *platform,
    const struct an7581_wifi_mt7996_completion_shared_memory *memory,
    uint32_t token_entry_count);
enum npu_runtime_result an7581_wifi_mt7996_completion_platform_initialize(
    struct an7581_wifi_mt7996_completion_platform *platform,
    const struct an7581_wifi_mt7996_completion_platform_config *config);
const struct an7581_wifi_mt7996_completion_lifecycle_operations *
an7581_wifi_mt7996_completion_platform_operations(void);

#endif

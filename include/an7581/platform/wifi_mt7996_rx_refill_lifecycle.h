/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_H
#define AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_H

#include "an7581/platform/core1_dispatch.h"
#include "an7581/platform/wifi_mt7996_rro_control_lifecycle.h"
#include "an7581/services/wifi/rx_refill.h"

#define AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT                                \
  NPU_WIFI_RX_REFILL_WORKER_MAX_RINGS
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_PACKET_BUFFER (UINT32_C(1) << 0)
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BUFFER (UINT32_C(1) << 1)
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_RRO_BAND0 (UINT32_C(1) << 2)
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_RRO_BAND2 (UINT32_C(1) << 3)
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BAND0 (UINT32_C(1) << 4)
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BAND1 (UINT32_C(1) << 5)
#define AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BAND2 (UINT32_C(1) << 6)

enum an7581_wifi_mt7996_rx_refill_lifecycle_state {
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_UNINITIALIZED = 0,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAITING_FOR_CONFIGURATION,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_ACTIVATION_GATED,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAITING_FOR_CONTROL_PLANE,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_BINDING_RINGS,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_PUBLISHING,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAKING_WORKER,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_RETRYABLE_FAILURE,
  AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_ACTIVE,
};

struct an7581_wifi_mt7996_rx_refill_configuration_readiness {
  uint32_t missing;
  uint32_t invalid;
};

typedef bool (*an7581_wifi_mt7996_rx_refill_read32)(void *context,
                                                    uint32_t address,
                                                    uint32_t *value);
typedef enum npu_runtime_result (*an7581_wifi_mt7996_rx_refill_worker_wake)(
    void *context, uint32_t hart_mask);

struct an7581_wifi_mt7996_rx_refill_operations {
  an7581_wifi_mt7996_rx_refill_read32 read32;
  npu_wifi_rx_refill_write32 write32;
  npu_wifi_rx_refill_worker_event_callback event;
  npu_wifi_rx_refill_worker_delay_callback delay;
  an7581_wifi_mt7996_rx_refill_worker_wake wake_worker;
};

struct an7581_wifi_mt7996_rx_refill_lifecycle_config {
  const struct npu_wifi_configuration *configuration;
  struct an7581_wifi_mt7996_rro_control_lifecycle *control_lifecycle;
  struct an7581_core1_dispatch *dispatch;
  const struct an7581_wifi_mt7996_rx_refill_operations *operations;
  struct npu_wifi_rx_refill_diagnostic_counters
      diagnostic_counters[AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT];
  void *operation_context;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_rx_refill_lifecycle {
  const struct npu_wifi_configuration *configuration;
  struct an7581_wifi_mt7996_rro_control_lifecycle *control_lifecycle;
  struct an7581_core1_dispatch *dispatch;
  const struct an7581_wifi_mt7996_rx_refill_operations *operations;
  void *operation_context;
  struct npu_wifi_rx_refill_diagnostic_counters
      diagnostic_counters[AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT];
  struct npu_wifi_rx_refill_state
      states[AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT];
  struct npu_wifi_rx_refill_worker_ring
      rings[AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT];
  enum an7581_wifi_mt7996_rx_refill_lifecycle_state state;
  enum npu_runtime_result last_status;
  uint32_t step_count;
  uint32_t configuration_wait_count;
  uint32_t activation_gate_count;
  uint32_t control_plane_wait_count;
  uint32_t ring_bind_attempt_count;
  uint32_t publication_attempt_count;
  uint32_t wake_attempt_count;
  uint32_t worker_step_count;
  uint32_t worker_failure_count;
  uint32_t retryable_failure_count;
  bool activation_allowed;
  bool rings_bound;
  bool worker_published;
  bool worker_woken;
  bool initialized;
};

struct an7581_wifi_mt7996_rx_refill_lifecycle_result {
  struct an7581_wifi_mt7996_rx_refill_configuration_readiness readiness;
  enum an7581_wifi_mt7996_rx_refill_lifecycle_state state;
  enum npu_runtime_result status;
  bool waiting_for_configuration;
  bool activation_gated;
  bool waiting_for_control_plane;
  bool rings_bound;
  bool worker_published;
  bool worker_woken;
  bool active;
};

enum npu_runtime_result an7581_wifi_mt7996_rx_refill_configuration_readiness(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_rx_refill_configuration_readiness *readiness);
enum npu_runtime_result an7581_wifi_mt7996_rx_refill_lifecycle_initialize(
    struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_rx_refill_lifecycle_config *config);
enum npu_runtime_result an7581_wifi_mt7996_rx_refill_lifecycle_step(
    struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rx_refill_lifecycle_result *result);

#endif

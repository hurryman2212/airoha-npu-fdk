/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_PPE_RESULT_BUNDLE_H
#define AN7581_WIFI_MT7996_PPE_RESULT_BUNDLE_H

#include "an7581/platform/wifi_mt7996_completion_lifecycle.h"
#include "an7581/platform/wifi_mt7996_packet_queue.h"
#include "an7581/platform/wifi_mt7996_ppe_result.h"
#include "an7581/platform/wifi_mt7996_tdma_delivery.h"
#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/packet_id_pool.h"

#define AN7581_WIFI_MT7996_PPE_RESULT_HART UINT32_C(0)
#define AN7581_WIFI_MT7996_PPE_RESULT_MODE_ADDRESS UINT32_C(0x1fb52230)
#define AN7581_WIFI_MT7996_PPE_RESULT_FORMAT_ADDRESS UINT32_C(0x1fb521f0)
#define AN7581_WIFI_MT7996_PPE_RESULT_ROUTE_ADDRESS UINT32_C(0x1fb50fe8)
#define AN7581_WIFI_MT7996_PPE_RESULT_MODE_VALUE UINT32_C(3)
#define AN7581_WIFI_MT7996_PPE_RESULT_FORMAT_VALUE UINT32_C(0x80048004)
#define AN7581_WIFI_MT7996_PPE_RESULT_ROUTE_PRESERVE_MASK UINT32_C(0xfff300ff)
#define AN7581_WIFI_MT7996_PPE_RESULT_ROUTE_VALUE UINT32_C(0x00190100)

struct an7581_wifi_mt7996_ppe_result_bundle_memory {
  struct an7581_wifi_mt7996_packet_queue_memory packet_queue;
  struct an7581_wifi_mt7996_tdma_delivery_memory tdma_delivery;
  volatile uint8_t *packet_mapping;
  volatile struct an7581_wifi_mt7996_ppe_result_registers *result_registers;
  volatile uint32_t *result_mode;
  volatile uint32_t *result_format;
  volatile uint32_t *result_route;
  size_t packet_mapping_size;
  uint32_t packet_dma_base;
  uint32_t packet_count;
};

struct an7581_wifi_mt7996_ppe_result_bundle_config {
  struct an7581_wifi_mt7996_ppe_result_bundle_memory memory;
  struct npu_wifi_packet_id_pool *packet_pool;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *diagnostic_counters;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *tdma_diagnostic_counters;
  uint32_t hart_id;
  uint16_t packet_queue_producer;
  uint16_t fragment_queue_producer;
};

struct an7581_wifi_mt7996_ppe_result_bundle {
  struct an7581_wifi_mt7996_packet_queue_platform packet_queue;
  struct an7581_wifi_mt7996_tdma_delivery_platform tdma_delivery;
  struct an7581_wifi_mt7996_ppe_result_platform result;
  struct npu_wifi_packet_id_pool *packet_pool;
  bool initialized;
};

enum an7581_wifi_mt7996_ppe_result_lifecycle_state {
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_UNINITIALIZED = 0,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_CONFIGURATION,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVATION_GATED,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_COMPLETION,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_INITIALIZING,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVATING,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_RETRYABLE_FAILURE,
  AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVE,
};

struct an7581_wifi_mt7996_ppe_result_lifecycle_config {
  const struct npu_wifi_configuration *configuration;
  struct npu_wifi_packet_id_pool *packet_pool;
  const struct an7581_wifi_mt7996_completion_lifecycle *completion_lifecycle;
  const struct an7581_wifi_mt7996_ppe_result_bundle_memory *memory;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *diagnostic_counters;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *tdma_diagnostic_counters;
  uint32_t hart_id;
  uint16_t packet_queue_producer;
  uint16_t fragment_queue_producer;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_ppe_result_lifecycle {
  const struct npu_wifi_configuration *configuration;
  struct npu_wifi_packet_id_pool *packet_pool;
  const struct an7581_wifi_mt7996_completion_lifecycle *completion_lifecycle;
  const struct an7581_wifi_mt7996_ppe_result_bundle_memory *memory;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *diagnostic_counters;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *tdma_diagnostic_counters;
  struct an7581_wifi_mt7996_ppe_result_bundle bundle;
  enum an7581_wifi_mt7996_ppe_result_lifecycle_state state;
  enum npu_runtime_result last_status;
  uint32_t hart_id;
  uint32_t step_count;
  uint32_t configuration_wait_count;
  uint32_t activation_gate_count;
  uint32_t completion_wait_count;
  uint32_t initialization_attempt_count;
  uint32_t activation_attempt_count;
  uint32_t retryable_failure_count;
  uint16_t packet_queue_producer;
  uint16_t fragment_queue_producer;
  bool activation_allowed;
  bool initialized;
};

struct an7581_wifi_mt7996_ppe_result_lifecycle_result {
  enum an7581_wifi_mt7996_ppe_result_lifecycle_state state;
  enum npu_runtime_result status;
  bool waiting_for_configuration;
  bool activation_gated;
  bool waiting_for_completion;
  bool bundle_initialized;
  bool active;
};

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_bundle_memory_resolve(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_ppe_result_bundle_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_bundle_initialize(
    struct an7581_wifi_mt7996_ppe_result_bundle *bundle,
    const struct an7581_wifi_mt7996_ppe_result_bundle_config *config);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_bundle_set_active(
    struct an7581_wifi_mt7996_ppe_result_bundle *bundle, bool active);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_lifecycle_initialize(
    struct an7581_wifi_mt7996_ppe_result_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_ppe_result_lifecycle_config *config);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_lifecycle_step(
    struct an7581_wifi_mt7996_ppe_result_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_ppe_result_lifecycle_result *result);

#endif

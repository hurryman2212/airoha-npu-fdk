/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_RESET_H
#define NPU_WIFI_RRO_RESET_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define NPU_WIFI_RRO_RESET_DRAIN_DELAY UINT32_C(10)
#define NPU_WIFI_RRO_RESET_IDLE_DELAY UINT32_C(100)

typedef enum npu_runtime_result (*npu_wifi_rro_reset_delay)(void *context,
                                                            uint32_t duration);
typedef enum npu_runtime_result (*npu_wifi_rro_reset_operation)(void *context);

struct npu_wifi_rro_reset_config {
  const volatile uint32_t *result_target;
  const volatile uint32_t *result_observed;
  const volatile uint32_t *allocator_activity;
  npu_wifi_rro_reset_delay delay;
  npu_wifi_rro_reset_operation reset_packet_ids;
  npu_wifi_rro_reset_operation reset_buffer_ids;
  void *delay_context;
  void *packet_id_context;
  void *buffer_id_context;
  uint32_t poll_limit;
};

struct npu_wifi_rro_reset {
  const volatile uint32_t *result_target;
  const volatile uint32_t *result_observed;
  const volatile uint32_t *allocator_activity;
  npu_wifi_rro_reset_delay delay;
  npu_wifi_rro_reset_operation reset_packet_ids;
  npu_wifi_rro_reset_operation reset_buffer_ids;
  void *delay_context;
  void *packet_id_context;
  void *buffer_id_context;
  uint32_t poll_limit;
  uint32_t last_drain_poll_count;
  uint32_t last_activity_poll_count;
  uint32_t completed_reset_count;
  uint32_t timeout_count;
  uint32_t failure_count;
};

enum npu_runtime_result
npu_wifi_rro_reset_initialize(struct npu_wifi_rro_reset *reset,
                              const struct npu_wifi_rro_reset_config *config);
enum npu_runtime_result npu_wifi_rro_reset_apply(void *context);

#endif

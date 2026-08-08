/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_RUNTIME_H
#define NPU_WIFI_RRO_RUNTIME_H

#include "an7581/services/wifi/rro_cpu_queue.h"
#include "an7581/services/wifi/rro_descriptor.h"

struct npu_wifi_rro_runtime_config {
  struct npu_wifi_rro_cpu_queue *cpu_queue;
  struct npu_wifi_rro_indication_state *indication;
  struct npu_wifi_rro_descriptor_service *descriptor;
  const volatile uint32_t *ring_enabled;
  const volatile uint32_t *configuration_ready;
  volatile uint32_t *indication_attempt_counter;
  const struct npu_wifi_rro_cpu_queue_operations *cpu_queue_operations;
  void *cpu_queue_context;
  npu_wifi_rro_indication_write32 write32;
  void *write_context;
  uint32_t cpu_queue_budget;
  uint32_t indication_budget;
};

struct npu_wifi_rro_runtime {
  struct npu_wifi_rro_cpu_queue *cpu_queue;
  struct npu_wifi_rro_indication_state *indication;
  struct npu_wifi_rro_descriptor_service *descriptor;
  struct npu_wifi_rro_cpu_queue_operations cpu_queue_operations;
  const volatile uint32_t *ring_enabled;
  const volatile uint32_t *configuration_ready;
  volatile uint32_t *indication_attempt_counter;
  void *cpu_queue_context;
  npu_wifi_rro_indication_write32 write32;
  void *write_context;
  uint32_t cpu_queue_budget;
  uint32_t indication_budget;
};

struct npu_wifi_rro_runtime_step_result {
  uint32_t completed_count;
  bool waiting_for_ring;
  bool waiting_for_configuration;
  bool pending_work;
  bool idle;
  bool should_backoff;
};

enum npu_runtime_result npu_wifi_rro_runtime_initialize(
    struct npu_wifi_rro_runtime *runtime,
    const struct npu_wifi_rro_runtime_config *config);
bool npu_wifi_rro_runtime_is_configured(
    const struct npu_wifi_rro_runtime *runtime);
enum npu_runtime_result npu_wifi_rro_runtime_step_cpu_queue(
    struct npu_wifi_rro_runtime *runtime,
    struct npu_wifi_rro_runtime_step_result *result);
enum npu_runtime_result npu_wifi_rro_runtime_step_indication(
    struct npu_wifi_rro_runtime *runtime,
    struct npu_wifi_rro_runtime_step_result *result);

#endif

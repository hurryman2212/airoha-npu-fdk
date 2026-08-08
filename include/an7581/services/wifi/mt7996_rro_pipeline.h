/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_RRO_PIPELINE_H
#define NPU_WIFI_MT7996_RRO_PIPELINE_H

#include "an7581/services/wifi/backend_bundle.h"
#include "an7581/services/wifi/rro_control.h"
#include "an7581/services/wifi/rro_packet_backend.h"
#include "an7581/services/wifi/rro_page_backend.h"
#include "an7581/services/wifi/rro_quiesce.h"
#include "an7581/services/wifi/rro_reset.h"
#include "an7581/services/wifi/rro_runtime.h"

#define NPU_WIFI_MT7996_RRO_BACKEND_COUNT 2U

enum npu_wifi_mt7996_rro_backend_index {
  NPU_WIFI_MT7996_RRO_PACKET_BACKEND = 0,
  NPU_WIFI_MT7996_RRO_CONTROL_BACKEND,
};

struct npu_wifi_rro_memory_binding {
  void *memory;
  size_t size;
};

struct npu_wifi_mt7996_tdma_delivery;

struct npu_wifi_mt7996_rro_memory {
  struct npu_wifi_rro_memory_binding normal_groups;
  struct npu_wifi_rro_memory_binding icv_errors;
  struct npu_wifi_rro_memory_binding indication_descriptors;
  struct npu_wifi_rro_memory_binding metadata_records;
  struct npu_wifi_rro_memory_binding metadata_trailers;
  struct npu_wifi_rro_memory_binding page_release_queue;
  struct npu_wifi_rro_memory_binding cpu_queue;
  struct npu_wifi_rro_memory_binding packet_buffers;
  const volatile uint32_t *result_target;
  const volatile uint32_t *result_observed;
  const volatile uint32_t *allocator_activity;
};

struct npu_wifi_mt7996_rro_pipeline_config {
  struct npu_wifi_configuration *configuration;
  struct npu_wifi_mt7996_rro_memory memory;
  const struct npu_wifi_rro_cpu_queue_operations *packet_operations;
  volatile uint32_t *indication_attempt_counter;
  volatile uint32_t *indication_available_counter;
  volatile uint32_t *table_generation_mismatch_counter;
  volatile uint32_t *metadata_page_delay_counter;
  volatile uint32_t *routed_record_counter;
  volatile uint32_t *packet_queue_release_counter;
  volatile uint32_t *metadata_page_release_counter;
  struct npu_wifi_rro_cpu_queue_diagnostic_counters
      cpu_queue_diagnostic_counters;
  struct npu_wifi_mt7996_tdma_delivery *tdma_delivery;
  npu_wifi_rro_control_map_table map_table;
  npu_wifi_rro_cache_discard discard_cache;
  npu_wifi_rro_cursor_publish publish_cursor;
  npu_wifi_rro_indication_write32 write32;
  npu_wifi_rro_reset_delay delay;
  npu_wifi_rro_reset_operation reset_packet_ids;
  npu_wifi_rro_reset_operation reset_buffer_ids;
  void *map_context;
  void *discard_context;
  void *cursor_context;
  void *write_context;
  void *packet_context;
  void *delay_context;
  void *packet_id_context;
  void *buffer_id_context;
  uint32_t page_pool_base;
  uint32_t page_count;
  uint32_t indication_register_base;
  uint32_t packet_buffer_count;
  uint32_t item_budget;
  uint32_t record_budget;
  uint32_t cpu_queue_budget;
  uint32_t indication_budget;
  uint32_t reset_poll_limit;
  uint32_t information_interface;
  uint16_t cpu_queue_producer;
  uint16_t cpu_queue_consumer;
  bool normal_cpu_queue_enabled;
};

struct npu_wifi_mt7996_rro_pipeline {
  struct npu_wifi_rro_table_backend table_backend;
  struct npu_wifi_rro_packet_backend packet_backend;
  struct npu_wifi_rro_packet_service packet;
  struct npu_wifi_rro_cpu_queue cpu_queue;
  struct npu_wifi_rro_page_backend page_backend;
  struct npu_wifi_rro_descriptor_service descriptor;
  struct npu_wifi_rro_indication_state indication;
  struct npu_wifi_rro_reset reset;
  struct npu_wifi_rro_quiesce quiesce;
  struct npu_wifi_rro_control control;
  struct npu_wifi_rro_runtime runtime;
  struct npu_wifi_backend_binding
      backend_bindings[NPU_WIFI_MT7996_RRO_BACKEND_COUNT];
  bool page_pool_configured;
  bool initialized;
};

enum npu_runtime_result npu_wifi_mt7996_rro_pipeline_initialize(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_mt7996_rro_pipeline_config *config);
enum npu_runtime_result npu_wifi_mt7996_rro_pipeline_get_backend_bindings(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_backend_binding **bindings, size_t *binding_count);

#endif

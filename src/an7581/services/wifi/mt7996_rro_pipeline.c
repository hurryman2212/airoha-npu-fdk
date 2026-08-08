/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_rro_pipeline.h"

#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/mt7996_tdma_delivery.h"

static bool size_multiply(uint32_t count, size_t element_size, size_t *size) {
  if (size == NULL || (size_t)count > SIZE_MAX / element_size)
    return false;

  *size = (size_t)count * element_size;
  return true;
}

static bool binding_is_valid(const struct npu_wifi_rro_memory_binding *binding,
                             size_t required_size, size_t alignment) {
  return binding != NULL && binding->memory != NULL &&
         ((uintptr_t)binding->memory & (alignment - 1U)) == 0U &&
         binding->size >= required_size;
}

static bool
reset_words_are_valid(const struct npu_wifi_mt7996_rro_memory *memory) {
  return memory->result_target != NULL && memory->result_observed != NULL &&
         memory->allocator_activity != NULL &&
         ((uintptr_t)memory->result_target & (sizeof(uint32_t) - 1U)) == 0U &&
         ((uintptr_t)memory->result_observed & (sizeof(uint32_t) - 1U)) == 0U &&
         ((uintptr_t)memory->allocator_activity & (sizeof(uint32_t) - 1U)) ==
             0U;
}

static bool page_pool_address_is_valid(uint32_t address, uint32_t page_count) {
  uint32_t pool_size;

  if (address == 0U ||
      (address & (NPU_WIFI_RRO_METADATA_PAGE_SIZE - 1U)) != 0U ||
      page_count == 0U || page_count > UINT32_C(0x10000))
    return false;
  pool_size = page_count * NPU_WIFI_RRO_METADATA_PAGE_SIZE;
  return address <= NPU_WIFI_RRO_PAGE_POOL_ADDRESS_LIMIT - pool_size + 1U;
}

static bool
operations_are_valid(const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  return config->packet_operations != NULL &&
         config->packet_operations->dispatch != NULL &&
         config->packet_operations->dispatch_special != NULL &&
         config->packet_operations->release != NULL &&
         config->map_table != NULL && config->discard_cache != NULL &&
         config->publish_cursor != NULL && config->write32 != NULL &&
         config->delay != NULL && config->reset_packet_ids != NULL &&
         config->reset_buffer_ids != NULL;
}

static enum npu_runtime_result resolve_packet_binding(
    const struct npu_wifi_mt7996_rro_pipeline_config *config,
    struct npu_wifi_mt7996_rro_pipeline_config *resolved_config) {
  bool has_explicit_operations = config->packet_operations != NULL;
  bool has_tdma_delivery = config->tdma_delivery != NULL;

  if (has_explicit_operations == has_tdma_delivery)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *resolved_config = *config;
  if (has_tdma_delivery) {
    if (config->packet_context != NULL)
      return NPU_RUNTIME_INVALID_ARGUMENT;
    if (!config->tdma_delivery->initialized)
      return NPU_RUNTIME_OUT_OF_RANGE;
    resolved_config->packet_operations = &npu_wifi_mt7996_tdma_rro_operations;
    resolved_config->packet_context = config->tdma_delivery;
  }
  return operations_are_valid(resolved_config) ? NPU_RUNTIME_SUCCESS
                                               : NPU_RUNTIME_INVALID_ARGUMENT;
}

static bool
memory_is_valid(const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  const struct npu_wifi_mt7996_rro_memory *memory = &config->memory;
  size_t metadata_size;
  size_t packet_buffer_size;

  if (!size_multiply(config->page_count, NPU_WIFI_RRO_METADATA_PAGE_SIZE,
                     &metadata_size) ||
      !size_multiply(config->packet_buffer_count,
                     NPU_WIFI_RRO_PACKET_BUFFER_STRIDE, &packet_buffer_size))
    return false;

  return binding_is_valid(
             &memory->normal_groups,
             NPU_WIFI_RRO_NORMAL_TABLE_GROUP_LIMIT *
                 sizeof(volatile struct npu_wifi_rro_metadata_table_entry *),
             sizeof(void *)) &&
         binding_is_valid(&memory->icv_errors,
                          NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT *
                              sizeof(uint32_t),
                          sizeof(uint32_t)) &&
         binding_is_valid(&memory->indication_descriptors,
                          NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT *
                              sizeof(struct npu_wifi_rro_indication_descriptor),
                          sizeof(uint32_t)) &&
         binding_is_valid(&memory->metadata_records, metadata_size,
                          sizeof(uint32_t)) &&
         binding_is_valid(&memory->metadata_trailers, metadata_size,
                          sizeof(uint32_t)) &&
         binding_is_valid(&memory->page_release_queue,
                          NPU_WIFI_RRO_PAGE_RELEASE_QUEUE_SIZE *
                              sizeof(uint16_t),
                          sizeof(uint16_t)) &&
         binding_is_valid(&memory->cpu_queue,
                          NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT *
                              sizeof(struct npu_wifi_rro_cpu_queue_entry),
                          sizeof(uint32_t)) &&
         binding_is_valid(&memory->packet_buffers, packet_buffer_size,
                          sizeof(uint32_t)) &&
         reset_words_are_valid(memory);
}

static bool scalar_configuration_is_valid(
    const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  return config->page_count != 0U && config->packet_buffer_count != 0U &&
         config->item_budget != 0U && config->record_budget != 0U &&
         config->cpu_queue_budget != 0U && config->indication_budget != 0U &&
         config->reset_poll_limit >= 2U &&
         config->information_interface < NPU_WIFI_INTERFACE_COUNT;
}

static enum npu_runtime_result initialize_storage_services(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  enum npu_runtime_result status;

  status = npu_wifi_rro_table_backend_initialize(
      &pipeline->table_backend, config->memory.normal_groups.memory,
      NPU_WIFI_RRO_NORMAL_TABLE_GROUP_LIMIT,
      NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT, NULL,
      NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  status = npu_wifi_rro_packet_backend_initialize(
      &pipeline->packet_backend, config->memory.packet_buffers.memory,
      config->packet_buffer_count, config->packet_operations->dispatch,
      config->packet_context, config->packet_operations->release,
      config->packet_context);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = npu_wifi_rro_packet_initialize(
      &pipeline->packet, &npu_wifi_rro_packet_backend_operations,
      &pipeline->packet_backend, config->routed_record_counter,
      config->packet_queue_release_counter);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  status = npu_wifi_rro_cpu_queue_initialize(
      &pipeline->cpu_queue, config->memory.cpu_queue.memory,
      config->memory.cpu_queue.size, &config->cpu_queue_diagnostic_counters,
      config->cpu_queue_producer, config->cpu_queue_consumer);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_wifi_rro_packet_configure_cpu_queue(
      &pipeline->packet, npu_wifi_rro_cpu_queue_enqueue, &pipeline->cpu_queue,
      config->normal_cpu_queue_enabled);
}

static enum npu_runtime_result initialize_descriptor_services(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  const struct npu_wifi_rro_item_operations item_operations = {
      .table = npu_wifi_rro_table_backend_operations,
      .page = npu_wifi_rro_page_backend_operations,
  };
  const struct npu_wifi_rro_item_contexts item_contexts = {
      .table = &pipeline->table_backend,
      .page = &pipeline->page_backend,
  };
  enum npu_runtime_result status;

  status = npu_wifi_rro_page_backend_initialize(
      &pipeline->page_backend, config->page_pool_base, config->page_count,
      config->memory.metadata_records.memory,
      config->memory.metadata_trailers.memory,
      config->memory.page_release_queue.memory,
      config->metadata_page_release_counter, config->discard_cache,
      config->discard_context, npu_wifi_rro_packet_consume, &pipeline->packet);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = npu_wifi_rro_descriptor_initialize(
      &pipeline->descriptor, config->page_pool_base, config->page_count,
      config->item_budget, config->record_budget,
      config->table_generation_mismatch_counter,
      config->metadata_page_delay_counter, &item_operations, &item_contexts,
      config->publish_cursor, config->cursor_context);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = npu_wifi_rro_descriptor_set_prepare(
      &pipeline->descriptor, npu_wifi_rro_packet_prepare_descriptor,
      &pipeline->packet);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_wifi_rro_indication_initialize(
      &pipeline->indication, config->memory.indication_descriptors.memory,
      config->memory.indication_descriptors.size,
      NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT,
      config->indication_register_base, config->indication_available_counter);
}

static enum npu_runtime_result initialize_reset_service(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  const struct npu_wifi_rro_reset_config reset_config = {
      .result_target = config->memory.result_target,
      .result_observed = config->memory.result_observed,
      .allocator_activity = config->memory.allocator_activity,
      .delay = config->delay,
      .reset_packet_ids = config->reset_packet_ids,
      .reset_buffer_ids = config->reset_buffer_ids,
      .delay_context = config->delay_context,
      .packet_id_context = config->packet_id_context,
      .buffer_id_context = config->buffer_id_context,
      .poll_limit = config->reset_poll_limit,
  };

  return npu_wifi_rro_reset_initialize(&pipeline->reset, &reset_config);
}

static enum npu_runtime_result set_page_pool_address(void *context,
                                                     uint32_t address) {
  struct npu_wifi_mt7996_rro_pipeline *pipeline = context;

  if (pipeline == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!page_pool_address_is_valid(address, pipeline->page_backend.page_count))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (pipeline->page_pool_configured)
    return pipeline->page_backend.page_pool_base == address
               ? NPU_RUNTIME_SUCCESS
               : NPU_RUNTIME_OWNERSHIP_ERROR;
  if (pipeline->descriptor.active ||
      pipeline->descriptor.item_state.phase != NPU_WIFI_RRO_ITEM_IDLE ||
      pipeline->descriptor.item_state.page_state.pending_release)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  pipeline->page_backend.page_pool_base = address;
  pipeline->descriptor.item_state.page_pool_base = address;
  pipeline->page_pool_configured = true;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result initialize_control_service(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  const struct npu_wifi_rro_quiesce_config quiesce_config = {
      .indication = &pipeline->indication,
      .descriptor = &pipeline->descriptor,
      .packet = &pipeline->packet,
      .cpu_queue = &pipeline->cpu_queue,
      .write32 = config->write32,
      .reset = npu_wifi_rro_reset_apply,
      .write_context = config->write_context,
      .reset_context = &pipeline->reset,
  };
  const struct npu_wifi_rro_control_config control_config = {
      .table_backend = &pipeline->table_backend,
      .icv_error_table = config->memory.icv_errors.memory,
      .map_table = config->map_table,
      .reset_buffers = npu_wifi_rro_quiesce_reset,
      .set_page_pool_address = set_page_pool_address,
      .map_context = config->map_context,
      .reset_context = &pipeline->quiesce,
      .page_pool_context = pipeline,
      .icv_error_word_count = NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT,
  };
  enum npu_runtime_result status;

  status = npu_wifi_rro_quiesce_initialize(&pipeline->quiesce, &quiesce_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = npu_wifi_rro_control_initialize(&pipeline->control, &control_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = npu_wifi_rro_control_bind_npu_information(
      &pipeline->control, config->configuration, config->information_interface);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_wifi_rro_control_bind_lifecycle(
      &pipeline->control, npu_wifi_rro_quiesce_prepare_stop,
      npu_wifi_rro_quiesce_resume, &pipeline->quiesce);
}

static enum npu_runtime_result
initialize_runtime(struct npu_wifi_mt7996_rro_pipeline *pipeline,
                   const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  const struct npu_wifi_rro_runtime_config runtime_config = {
      .cpu_queue = &pipeline->cpu_queue,
      .indication = &pipeline->indication,
      .descriptor = &pipeline->descriptor,
      .ring_enabled = &pipeline->control.ring_enabled,
      .configuration_ready = &pipeline->control.configuration_ready,
      .indication_attempt_counter = config->indication_attempt_counter,
      .cpu_queue_operations = config->packet_operations,
      .cpu_queue_context = config->packet_context,
      .write32 = config->write32,
      .write_context = config->write_context,
      .cpu_queue_budget = config->cpu_queue_budget,
      .indication_budget = config->indication_budget,
  };

  return npu_wifi_rro_runtime_initialize(&pipeline->runtime, &runtime_config);
}

enum npu_runtime_result npu_wifi_mt7996_rro_pipeline_initialize(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_mt7996_rro_pipeline_config *config) {
  struct npu_wifi_mt7996_rro_pipeline_config resolved_config;
  enum npu_runtime_result status;

  if (pipeline == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  status = resolve_packet_binding(config, &resolved_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  if (!scalar_configuration_is_valid(&resolved_config) ||
      !memory_is_valid(&resolved_config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(pipeline, 0U, sizeof(*pipeline));
  status = initialize_storage_services(pipeline, &resolved_config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_descriptor_services(pipeline, &resolved_config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_reset_service(pipeline, &resolved_config);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_control_service(pipeline, &resolved_config);
  if (status == NPU_RUNTIME_SUCCESS && resolved_config.page_pool_base != 0U)
    status = npu_wifi_rro_control_set_page_pool(&pipeline->control,
                                                resolved_config.page_pool_base);
  if (status == NPU_RUNTIME_SUCCESS)
    status = initialize_runtime(pipeline, &resolved_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  pipeline->backend_bindings[NPU_WIFI_MT7996_RRO_PACKET_BACKEND] =
      (struct npu_wifi_backend_binding){
          .operations = &npu_wifi_rro_packet_control_backend_operations,
          .context = &pipeline->packet,
      };
  pipeline->backend_bindings[NPU_WIFI_MT7996_RRO_CONTROL_BACKEND] =
      (struct npu_wifi_backend_binding){
          .operations = &npu_wifi_rro_control_backend_operations,
          .context = &pipeline->control,
      };
  pipeline->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_mt7996_rro_pipeline_get_backend_bindings(
    struct npu_wifi_mt7996_rro_pipeline *pipeline,
    const struct npu_wifi_backend_binding **bindings, size_t *binding_count) {
  if (pipeline == NULL || bindings == NULL || binding_count == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pipeline->initialized)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  *bindings = pipeline->backend_bindings;
  *binding_count = NPU_WIFI_MT7996_RRO_BACKEND_COUNT;
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_packet.h"

#include "an7581/runtime/memory.h"

enum npu_wifi_rro_queue_action {
  NPU_WIFI_RRO_QUEUE_WRITE_CONTROL = 0,
  NPU_WIFI_RRO_QUEUE_ENQUEUE,
  NPU_WIFI_RRO_QUEUE_ACTION_COUNT,
};

enum npu_runtime_result npu_wifi_rro_packet_initialize(
    struct npu_wifi_rro_packet_service *service,
    const struct npu_wifi_rro_fragment_operations *operations,
    void *operations_context, volatile uint32_t *routed_record_counter,
    volatile uint32_t *packet_queue_release_counter) {
  if (service == NULL || operations == NULL ||
      operations->write_control == NULL || operations->dispatch == NULL ||
      operations->release == NULL ||
      (routed_record_counter != NULL &&
       ((uintptr_t)routed_record_counter & (sizeof(uint32_t) - 1U)) != 0U) ||
      (packet_queue_release_counter != NULL &&
       ((uintptr_t)packet_queue_release_counter & (sizeof(uint32_t) - 1U)) !=
           0U))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(service, 0U, sizeof(*service));
  service->operations = *operations;
  service->operations_context = operations_context;
  service->routed_record_counter = routed_record_counter;
  service->packet_queue_release_counter = packet_queue_release_counter;
  npu_wifi_rro_fragment_initialize(&service->fragment_state);
  return NPU_RUNTIME_SUCCESS;
}

static bool packet_service_has_pending_work(
    const struct npu_wifi_rro_packet_service *service) {
  return service->fragment_state.active || service->fragment_state.discarding ||
         service->fragment_state.record_pending ||
         service->queue_record_pending;
}

enum npu_runtime_result npu_wifi_rro_packet_configure_cpu_queue(
    struct npu_wifi_rro_packet_service *service,
    npu_wifi_rro_packet_enqueue enqueue, void *enqueue_context, bool enabled) {
  if (service == NULL || (enabled && enqueue == NULL))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (packet_service_has_pending_work(service))
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  service->enqueue = enqueue;
  service->enqueue_context = enqueue_context;
  service->normal_queue_enabled = enabled;
  if (!enabled)
    service->use_cpu_queue = false;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_packet_set_force_to_cpu(
    struct npu_wifi_rro_packet_service *service, bool force_to_cpu) {
  if (service == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (packet_service_has_pending_work(service))
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  service->force_to_cpu = force_to_cpu;
  if (force_to_cpu)
    service->use_cpu_queue = false;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_packet_prepare_descriptor(
    void *context, const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t descriptor_index) {
  struct npu_wifi_rro_packet_service *service = context;
  uint32_t route_bits;

  (void)descriptor_index;
  if (service == NULL || descriptor == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (packet_service_has_pending_work(service))
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  route_bits = descriptor->sequence_control & NPU_WIFI_RRO_PACKET_ROUTE_MASK;
  if (route_bits != NPU_WIFI_RRO_PACKET_ROUTE_ONE &&
      route_bits != NPU_WIFI_RRO_PACKET_ROUTE_TWO)
    route_bits = 0U;
  service->route_bits = route_bits;
  service->use_cpu_queue =
      !service->force_to_cpu && route_bits == 0U &&
      service->normal_queue_enabled &&
      (descriptor->sequence_control & NPU_WIFI_RRO_TABLE_SELECTOR_MASK) !=
          NPU_WIFI_RRO_SPECIAL_TABLE_SELECTOR;
  return NPU_RUNTIME_SUCCESS;
}

static bool
records_match(const struct npu_wifi_rro_metadata_record_fields *left,
              const struct npu_wifi_rro_metadata_record_fields *right) {
  return left->packet_control == right->packet_control &&
         left->buffer_id == right->buffer_id &&
         left->signed_buffer_id == right->signed_buffer_id &&
         left->packet_length == right->packet_length &&
         left->fragment_type == right->fragment_type &&
         left->special_data_offset_units == right->special_data_offset_units &&
         left->last_segment == right->last_segment &&
         left->packet_control_flag == right->packet_control_flag;
}

static bool is_standalone_terminal(
    const struct npu_wifi_rro_packet_service *service,
    const struct npu_wifi_rro_metadata_record_fields *fields) {
  return !service->fragment_state.active &&
         !service->fragment_state.discarding &&
         !service->fragment_state.record_pending &&
         (fields->packet_control & 1U) == 0U;
}

static bool
should_apply_route(const struct npu_wifi_rro_packet_service *service,
                   const struct npu_wifi_rro_metadata_record_fields *fields) {
  if (service->route_bits == 0U)
    return false;
  if (service->fragment_state.record_pending)
    return (service->fragment_state.pending_record.packet_control &
            NPU_WIFI_RRO_PACKET_ROUTE_MASK) == service->route_bits;
  return is_standalone_terminal(service, fields);
}

static void
set_queue_result_state(struct npu_wifi_rro_packet_service *service) {
  service->last_result.fragment_count = service->fragment_state.fragment_count;
  service->last_result.active = service->fragment_state.active;
  service->last_result.discarding = service->fragment_state.discarding;
}

static enum npu_runtime_result process_cpu_queue_record(
    struct npu_wifi_rro_packet_service *service,
    const struct npu_wifi_rro_metadata_record_fields *fields) {
  enum npu_runtime_result status;

  (void)npu_memset(&service->last_result, 0U, sizeof(service->last_result));
  if (service->queue_record_pending) {
    if (!records_match(&service->pending_queue_record, fields)) {
      set_queue_result_state(service);
      return NPU_RUNTIME_OWNERSHIP_ERROR;
    }
  } else {
    service->pending_queue_record = *fields;
    service->queue_action_index = NPU_WIFI_RRO_QUEUE_WRITE_CONTROL;
    service->queue_record_pending = true;
  }

  if (service->queue_action_index == NPU_WIFI_RRO_QUEUE_WRITE_CONTROL) {
    status = service->operations.write_control(
        service->operations_context, fields->buffer_id, fields->packet_control);
    if (status != NPU_RUNTIME_SUCCESS) {
      set_queue_result_state(service);
      return status;
    }
    service->queue_action_index = NPU_WIFI_RRO_QUEUE_ENQUEUE;
    ++service->last_result.actions_completed;
  }
  if (service->queue_action_index == NPU_WIFI_RRO_QUEUE_ENQUEUE) {
    status = service->enqueue(service->enqueue_context, fields->buffer_id,
                              fields->packet_control);
    if (status != NPU_RUNTIME_SUCCESS) {
      set_queue_result_state(service);
      return status;
    }
    service->queue_action_index = NPU_WIFI_RRO_QUEUE_ACTION_COUNT;
    ++service->last_result.actions_completed;
  }

  service->queue_action_index = NPU_WIFI_RRO_QUEUE_WRITE_CONTROL;
  service->queue_record_pending = false;
  service->last_result.record_committed = true;
  set_queue_result_state(service);
  ++service->queued_record_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_packet_consume(
    void *context, const struct npu_wifi_rro_metadata_record *record,
    uint32_t record_index, uint32_t page_address, uint16_t page_slot) {
  struct npu_wifi_rro_packet_service *service = context;
  struct npu_wifi_rro_metadata_record_fields fields;
  enum npu_runtime_result status;
  bool standalone_terminal;
  bool routed = false;

  if (service == NULL || record == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (page_slot >= NPU_WIFI_RRO_METADATA_RECORDS_PER_PAGE)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = npu_wifi_rro_metadata_record_decode(record, &fields);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  standalone_terminal = is_standalone_terminal(service, &fields);
  if (service->queue_record_pending ||
      (service->use_cpu_queue && standalone_terminal)) {
    status = process_cpu_queue_record(service, &fields);
  } else {
    if (should_apply_route(service, &fields)) {
      fields.packet_control |= service->route_bits;
      routed = true;
      if (service->routed_record_counter != NULL)
        ++*service->routed_record_counter;
    }
    status = npu_wifi_rro_fragment_consume(
        &service->fragment_state, &fields, &service->operations,
        service->operations_context, &service->last_result);
    if (standalone_terminal && service->packet_queue_release_counter != NULL)
      *service->packet_queue_release_counter +=
          service->last_result.dispatch_rejections;
    if (status == NPU_RUNTIME_SUCCESS && routed)
      ++service->routed_record_count;
  }
  service->last_record = fields;
  service->last_record_index = record_index;
  service->last_page_address = page_address;
  service->last_page_slot = page_slot;
  if (status == NPU_RUNTIME_SUCCESS)
    ++service->committed_record_count;
  return status;
}

static bool set_force_to_cpu(void *context, bool force_to_cpu) {
  return npu_wifi_rro_packet_set_force_to_cpu(context, force_to_cpu) ==
         NPU_RUNTIME_SUCCESS;
}

const struct npu_wifi_backend_operations
    npu_wifi_rro_packet_control_backend_operations = {
        .set_force_to_cpu = set_force_to_cpu,
};

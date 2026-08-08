/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_descriptor.h"

#include "an7581/runtime/memory.h"

static bool
operations_valid(const struct npu_wifi_rro_item_operations *operations) {
  return operations != NULL && operations->table.read != NULL &&
         operations->table.delay_retry != NULL &&
         operations->table.invalidate != NULL && operations->page.map != NULL &&
         operations->page.discard != NULL && operations->page.consume != NULL &&
         operations->page.release != NULL;
}

enum npu_runtime_result npu_wifi_rro_descriptor_initialize(
    struct npu_wifi_rro_descriptor_service *service, uint32_t page_pool_base,
    uint32_t page_pool_count, uint32_t item_budget, uint32_t record_budget,
    volatile uint32_t *generation_mismatch_counter,
    volatile uint32_t *metadata_page_delay_counter,
    const struct npu_wifi_rro_item_operations *operations,
    const struct npu_wifi_rro_item_contexts *contexts,
    npu_wifi_rro_cursor_publish publish_cursor, void *publish_context) {
  enum npu_runtime_result status;

  if (service == NULL || !operations_valid(operations) || contexts == NULL ||
      publish_cursor == NULL || item_budget == 0U || record_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(service, 0U, sizeof(*service));
  status = npu_wifi_rro_item_initialize(
      &service->item_state, page_pool_base, page_pool_count,
      generation_mismatch_counter, metadata_page_delay_counter);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  service->operations = *operations;
  service->contexts = *contexts;
  service->publish_cursor = publish_cursor;
  service->publish_context = publish_context;
  service->item_budget = item_budget;
  service->record_budget = record_budget;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_descriptor_set_prepare(
    struct npu_wifi_rro_descriptor_service *service,
    npu_wifi_rro_descriptor_prepare prepare_descriptor, void *prepare_context) {
  if (service == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (service->active)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  service->prepare_descriptor = prepare_descriptor;
  service->prepare_context = prepare_context;
  return NPU_RUNTIME_SUCCESS;
}

static bool
descriptor_matches(const struct npu_wifi_rro_descriptor_service *service,
                   const struct npu_wifi_rro_indication_descriptor *descriptor,
                   uint32_t descriptor_index) {
  return service->descriptor.sequence_control == descriptor->sequence_control &&
         service->descriptor.count_control == descriptor->count_control &&
         service->descriptor_index == descriptor_index;
}

static uint32_t cursor_publication_value(
    const struct npu_wifi_rro_descriptor_service *service) {
  const struct npu_wifi_rro_metadata_cursor *cursor =
      &service->last_item_result.cursor;
  uint32_t cursor_value =
      ((uint32_t)cursor->generation << 10U) | cursor->table_slot;

  return (service->descriptor.sequence_control &
          NPU_WIFI_RRO_TABLE_SELECTOR_MASK) |
         (cursor_value << 16U);
}

static enum npu_runtime_result
publish_pending_cursor(struct npu_wifi_rro_descriptor_service *service) {
  enum npu_runtime_result status;

  if (!service->cursor_publication_pending)
    return NPU_RUNTIME_SUCCESS;
  status = service->publish_cursor(service->publish_context,
                                   service->pending_cursor_value);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  service->cursor_publication_pending = false;
  ++service->cursor_publication_count;
  return NPU_RUNTIME_SUCCESS;
}

static void finish_descriptor(struct npu_wifi_rro_descriptor_service *service) {
  service->active = false;
  service->descriptor_prepare_pending = false;
  service->item_offset = 0U;
  ++service->committed_descriptor_count;
}

static enum npu_runtime_result
prepare_pending_descriptor(struct npu_wifi_rro_descriptor_service *service) {
  enum npu_runtime_result status;

  if (!service->descriptor_prepare_pending)
    return NPU_RUNTIME_SUCCESS;
  status = service->prepare_descriptor(service->prepare_context,
                                       &service->descriptor,
                                       service->descriptor_index);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  service->descriptor_prepare_pending = false;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_descriptor_consume(
    void *context, const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t descriptor_index) {
  struct npu_wifi_rro_descriptor_service *service = context;
  enum npu_runtime_result status;
  uint32_t completed_this_call = 0U;
  uint32_t item_count;

  if (service == NULL || descriptor == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!service->active) {
    service->descriptor = *descriptor;
    service->descriptor_index = descriptor_index;
    service->item_offset = 0U;
    service->active = true;
    service->descriptor_prepare_pending = service->prepare_descriptor != NULL;
  } else if (!descriptor_matches(service, descriptor, descriptor_index)) {
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  status = prepare_pending_descriptor(service);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  item_count = npu_wifi_rro_indication_item_count(descriptor);
  status = publish_pending_cursor(service);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  if (service->item_offset == item_count) {
    finish_descriptor(service);
    return NPU_RUNTIME_SUCCESS;
  }

  while (completed_this_call < service->item_budget) {
    status = npu_wifi_rro_item_process(
        &service->item_state, descriptor, service->item_offset,
        service->record_budget, &service->operations, &service->contexts,
        &service->last_item_result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    if (!service->last_item_result.item_complete)
      return NPU_RUNTIME_EMPTY;

    ++service->item_offset;
    ++service->committed_item_count;
    ++completed_this_call;
    if ((service->item_offset & 3U) == 0U ||
        service->item_offset == item_count) {
      service->pending_cursor_value = cursor_publication_value(service);
      service->cursor_publication_pending = true;
      status = publish_pending_cursor(service);
      if (status != NPU_RUNTIME_SUCCESS)
        return status;
    }
    if (service->item_offset == item_count) {
      finish_descriptor(service);
      return NPU_RUNTIME_SUCCESS;
    }
  }

  return NPU_RUNTIME_EMPTY;
}

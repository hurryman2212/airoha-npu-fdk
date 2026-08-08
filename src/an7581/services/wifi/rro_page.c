/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_page.h"

#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct npu_wifi_rro_metadata_page) ==
                   NPU_WIFI_RRO_METADATA_PAGE_SIZE,
               "Wi-Fi RRO metadata page layout changed");

static const uint32_t metadata_page_readiness_poll_limit = 10U;
static const uint32_t metadata_page_readiness_delay_iterations = 100U;

static bool page_address_to_id(const struct npu_wifi_rro_page_state *state,
                               uint32_t page_address, uint16_t *page_id) {
  uint32_t offset;
  uint32_t id;

  if (page_address < state->page_pool_base)
    return false;
  offset = page_address - state->page_pool_base;
  if (offset % NPU_WIFI_RRO_METADATA_PAGE_SIZE != 0U)
    return false;
  id = offset / NPU_WIFI_RRO_METADATA_PAGE_SIZE;
  if (id >= state->page_pool_count || id > UINT16_MAX)
    return false;

  *page_id = (uint16_t)id;
  return true;
}

static void set_result_state(const struct npu_wifi_rro_page_state *state,
                             struct npu_wifi_rro_page_result *result) {
  result->current_page_address = state->current_page_address;
  result->record_index = state->record_index;
  result->page_slot = state->page_slot;
  result->complete = state->record_index == state->total_record_count &&
                     !state->pending_release;
}

enum npu_runtime_result npu_wifi_rro_page_initialize(
    struct npu_wifi_rro_page_state *state, uint32_t first_page_address,
    uint32_t page_pool_base, uint32_t page_pool_count, uint32_t record_count,
    volatile uint32_t *delay_counter) {
  uint16_t page_id;

  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (record_count > NPU_WIFI_RRO_METADATA_RECORD_COUNT_LIMIT ||
      page_pool_count == 0U || page_pool_count > UINT32_C(0x10000) ||
      (delay_counter != NULL &&
       ((uintptr_t)delay_counter & (sizeof(uint32_t) - 1U)) != 0U))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(state, 0U, sizeof(*state));
  state->page_pool_base = page_pool_base;
  state->page_pool_count = page_pool_count;
  state->total_record_count = record_count;
  state->delay_counter = delay_counter;
  if (record_count == 0U)
    return NPU_RUNTIME_SUCCESS;

  state->current_page_address = first_page_address;
  if (!page_address_to_id(state, first_page_address, &page_id)) {
    (void)npu_memset(state, 0U, sizeof(*state));
    return NPU_RUNTIME_OUT_OF_RANGE;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
release_pending_page(struct npu_wifi_rro_page_state *state,
                     const struct npu_wifi_rro_page_operations *operations,
                     void *context, struct npu_wifi_rro_page_result *result) {
  enum npu_runtime_result status;

  if (!state->pending_release)
    return NPU_RUNTIME_SUCCESS;

  status = operations->release(context, state->pending_release_id);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  state->pending_release = false;
  state->current_page_address = state->pending_next_page_address;
  state->pending_next_page_address = 0U;
  ++result->release_count;
  if (state->record_index != state->total_record_count) {
    uint16_t page_id;

    if (!page_address_to_id(state, state->current_page_address, &page_id))
      return NPU_RUNTIME_OUT_OF_RANGE;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
map_current_page(const struct npu_wifi_rro_page_state *state,
                 const struct npu_wifi_rro_page_operations *operations,
                 void *context, struct npu_wifi_rro_metadata_page_view *view) {
  enum npu_runtime_result status;

  status = operations->map(context, state->current_page_address, view);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  if (view->records == NULL || view->next_page_address == NULL ||
      view->readiness == NULL ||
      ((uintptr_t)view->records & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)view->next_page_address & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)view->readiness & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_IO_ERROR;
  return NPU_RUNTIME_SUCCESS;
}

static void
snapshot_record(const volatile struct npu_wifi_rro_metadata_record *source,
                struct npu_wifi_rro_metadata_record *record) {
  record->data0 = source->data0;
  record->data1 = source->data1;
  record->data2 = source->data2;
  record->data3 = source->data3;
  record->data4 = source->data4;
  record->data5 = source->data5;
}

enum npu_runtime_result npu_wifi_rro_page_process(
    struct npu_wifi_rro_page_state *state, uint32_t record_budget,
    const struct npu_wifi_rro_page_operations *operations, void *context,
    struct npu_wifi_rro_page_result *result) {
  enum npu_runtime_result status;

  if (state == NULL || operations == NULL || operations->map == NULL ||
      operations->discard == NULL || operations->consume == NULL ||
      operations->release == NULL || result == NULL || record_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (state->page_pool_count == 0U ||
      state->page_pool_count > UINT32_C(0x10000) ||
      state->total_record_count > NPU_WIFI_RRO_METADATA_RECORD_COUNT_LIMIT ||
      state->record_index > state->total_record_count ||
      state->page_slot >= NPU_WIFI_RRO_METADATA_RECORDS_PER_PAGE)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  status = release_pending_page(state, operations, context, result);
  set_result_state(state, result);
  if (status != NPU_RUNTIME_SUCCESS || result->complete)
    return status;

  while (result->consumed_count < record_budget &&
         state->record_index < state->total_record_count) {
    struct npu_wifi_rro_metadata_page_view view;
    struct npu_wifi_rro_metadata_record record;
    uint32_t discard_address;
    uint16_t page_id;

    if (!page_address_to_id(state, state->current_page_address, &page_id))
      return NPU_RUNTIME_OUT_OF_RANGE;

    status = map_current_page(state, operations, context, &view);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    if (state->page_slot == 0U) {
      uint32_t poll;

      for (poll = 0U; poll < metadata_page_readiness_poll_limit; ++poll) {
        volatile uint32_t delay;

        if (*view.readiness >= 0)
          break;
        result->page_not_ready = true;
        for (delay = 0U; delay < metadata_page_readiness_delay_iterations;
             ++delay) {
        }
      }
      if (result->page_not_ready && state->delay_counter != NULL)
        ++*state->delay_counter;
    }

    if (state->page_slot == 0U) {
      discard_address = state->current_page_address;
      status = operations->discard(context, discard_address);
      if (status != NPU_RUNTIME_SUCCESS)
        return status;
    } else if (state->page_slot == 2U) {
      if (state->current_page_address >
          UINT32_MAX - NPU_WIFI_RRO_SECOND_CACHE_LINE_OFFSET)
        return NPU_RUNTIME_OUT_OF_RANGE;
      discard_address =
          state->current_page_address + NPU_WIFI_RRO_SECOND_CACHE_LINE_OFFSET;
      status = operations->discard(context, discard_address);
      if (status != NPU_RUNTIME_SUCCESS)
        return status;
    }

    snapshot_record(&view.records[state->page_slot], &record);
    status = operations->consume(context, &record, state->record_index,
                                 state->current_page_address, state->page_slot);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;

    ++state->record_index;
    ++state->page_slot;
    ++result->consumed_count;
    if (state->page_slot == NPU_WIFI_RRO_METADATA_RECORDS_PER_PAGE ||
        state->record_index == state->total_record_count) {
      state->pending_next_page_address =
          state->page_slot == NPU_WIFI_RRO_METADATA_RECORDS_PER_PAGE
              ? *view.next_page_address
              : 0U;
      state->page_slot = 0U;
      state->pending_release_id = page_id;
      state->pending_release = true;

      status = release_pending_page(state, operations, context, result);
      set_result_state(state, result);
      if (status != NPU_RUNTIME_SUCCESS)
        return status;
    }
  }

  set_result_state(state, result);
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_item.h"

#include "an7581/runtime/memory.h"

enum npu_runtime_result
npu_wifi_rro_item_initialize(struct npu_wifi_rro_item_state *state,
                             uint32_t page_pool_base, uint32_t page_pool_count,
                             volatile uint32_t *generation_mismatch_counter,
                             volatile uint32_t *metadata_page_delay_counter) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (page_pool_count == 0U || page_pool_count > UINT32_C(0x10000) ||
      (generation_mismatch_counter != NULL &&
       ((uintptr_t)generation_mismatch_counter & (sizeof(uint32_t) - 1U)) !=
           0U) ||
      (metadata_page_delay_counter != NULL &&
       ((uintptr_t)metadata_page_delay_counter & (sizeof(uint32_t) - 1U)) !=
           0U))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(state, 0U, sizeof(*state));
  state->page_pool_base = page_pool_base;
  state->page_pool_count = page_pool_count;
  state->metadata_page_delay_counter = metadata_page_delay_counter;
  npu_wifi_rro_table_initialize(&state->table_state,
                                generation_mismatch_counter);
  return NPU_RUNTIME_SUCCESS;
}

static bool
request_matches(const struct npu_wifi_rro_item_state *state,
                const struct npu_wifi_rro_indication_descriptor *descriptor,
                uint32_t item_offset) {
  return state->descriptor.sequence_control == descriptor->sequence_control &&
         state->descriptor.count_control == descriptor->count_control &&
         state->item_offset == item_offset;
}

static bool
operations_valid(const struct npu_wifi_rro_item_operations *operations) {
  return operations != NULL && operations->table.read != NULL &&
         operations->table.delay_retry != NULL &&
         operations->table.invalidate != NULL && operations->page.map != NULL &&
         operations->page.discard != NULL && operations->page.consume != NULL &&
         operations->page.release != NULL;
}

static void set_result_cursor(const struct npu_wifi_rro_item_state *state,
                              struct npu_wifi_rro_item_result *result) {
  result->cursor = state->cursor;
}

enum npu_runtime_result npu_wifi_rro_item_process(
    struct npu_wifi_rro_item_state *state,
    const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t item_offset, uint32_t record_budget,
    const struct npu_wifi_rro_item_operations *operations,
    const struct npu_wifi_rro_item_contexts *contexts,
    struct npu_wifi_rro_item_result *result) {
  enum npu_runtime_result status;

  if (state == NULL || descriptor == NULL || !operations_valid(operations) ||
      contexts == NULL || result == NULL || record_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (state->page_pool_count == 0U ||
      state->page_pool_count > UINT32_C(0x10000))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  if (state->phase == NPU_WIFI_RRO_ITEM_IDLE) {
    status = npu_wifi_rro_metadata_cursor_decode(descriptor, item_offset,
                                                 &state->cursor);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    state->descriptor = *descriptor;
    state->item_offset = item_offset;
    state->phase = NPU_WIFI_RRO_ITEM_CLAIM_TABLE;
  } else if (!request_matches(state, descriptor, item_offset)) {
    set_result_cursor(state, result);
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }
  set_result_cursor(state, result);

  if (state->phase == NPU_WIFI_RRO_ITEM_CLAIM_TABLE) {
    struct npu_wifi_rro_table_result table_result;

    status = npu_wifi_rro_table_claim(&state->table_state, &state->cursor,
                                      &operations->table, contexts->table,
                                      &table_result);
    result->table_retry_performed = table_result.retry_performed;
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    if (table_result.skipped) {
      result->table_skipped = true;
      result->item_complete = true;
      state->phase = NPU_WIFI_RRO_ITEM_IDLE;
      return NPU_RUNTIME_SUCCESS;
    }

    status = npu_wifi_rro_page_initialize(
        &state->page_state, table_result.entry.page_address,
        state->page_pool_base, state->page_pool_count,
        table_result.entry.record_count, state->metadata_page_delay_counter);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    state->phase = table_result.entry.record_count == 0U
                       ? NPU_WIFI_RRO_ITEM_COMPLETE_TABLE
                       : NPU_WIFI_RRO_ITEM_PROCESS_PAGE;
  }

  if (state->phase == NPU_WIFI_RRO_ITEM_PROCESS_PAGE) {
    struct npu_wifi_rro_page_result page_result;

    status = npu_wifi_rro_page_process(&state->page_state, record_budget,
                                       &operations->page, contexts->page,
                                       &page_result);
    result->records_consumed = page_result.consumed_count;
    result->pages_released = page_result.release_count;
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    if (!page_result.complete)
      return NPU_RUNTIME_SUCCESS;
    state->phase = NPU_WIFI_RRO_ITEM_COMPLETE_TABLE;
  }

  status = npu_wifi_rro_table_complete(&state->table_state, &operations->table,
                                       contexts->table);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  state->phase = NPU_WIFI_RRO_ITEM_IDLE;
  result->item_complete = true;
  return NPU_RUNTIME_SUCCESS;
}

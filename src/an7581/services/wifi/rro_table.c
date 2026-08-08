/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_table.h"

#include "an7581/runtime/memory.h"

void npu_wifi_rro_table_initialize(
    struct npu_wifi_rro_table_state *state,
    volatile uint32_t *generation_mismatch_counter) {
  if (state != NULL) {
    (void)npu_memset(state, 0U, sizeof(*state));
    state->generation_mismatch_counter = generation_mismatch_counter;
  }
}

static bool cursors_match(const struct npu_wifi_rro_metadata_cursor *left,
                          const struct npu_wifi_rro_metadata_cursor *right) {
  return left->table_selector == right->table_selector &&
         left->table_group == right->table_group &&
         left->table_entry_index == right->table_entry_index &&
         left->table_slot == right->table_slot &&
         left->generation == right->generation &&
         left->uses_special_table == right->uses_special_table;
}

static void set_result(const struct npu_wifi_rro_table_state *state,
                       struct npu_wifi_rro_table_result *result) {
  result->entry = state->entry;
  result->retry_performed = state->retry_performed;
  result->claimed = state->phase == NPU_WIFI_RRO_TABLE_CLAIMED;
}

static enum npu_runtime_result
read_entry(struct npu_wifi_rro_table_state *state,
           const struct npu_wifi_rro_table_operations *operations,
           void *context) {
  struct npu_wifi_rro_metadata_table_entry entry;
  enum npu_runtime_result status;

  status = operations->read(context, &state->cursor, &entry);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_wifi_rro_metadata_table_entry_decode(&entry, &state->cursor,
                                                  &state->entry);
}

static enum npu_runtime_result
invalidate_mismatch(struct npu_wifi_rro_table_state *state,
                    const struct npu_wifi_rro_table_operations *operations,
                    void *context, struct npu_wifi_rro_table_result *result) {
  enum npu_runtime_result status;

  if (state->generation_mismatch_counter != NULL)
    ++*state->generation_mismatch_counter;
  status = operations->invalidate(context, &state->cursor);

  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  result->entry = state->entry;
  result->retry_performed = state->retry_performed;
  result->skipped = true;
  result->invalidated = true;
  state->phase = NPU_WIFI_RRO_TABLE_IDLE;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_wifi_rro_table_claim(struct npu_wifi_rro_table_state *state,
                         const struct npu_wifi_rro_metadata_cursor *cursor,
                         const struct npu_wifi_rro_table_operations *operations,
                         void *context,
                         struct npu_wifi_rro_table_result *result) {
  enum npu_runtime_result status;

  if (state == NULL || cursor == NULL || operations == NULL ||
      operations->read == NULL || operations->delay_retry == NULL ||
      operations->invalidate == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (cursor->generation > 3U ||
      (state->generation_mismatch_counter != NULL &&
       ((uintptr_t)state->generation_mismatch_counter &
        (sizeof(uint32_t) - 1U)) != 0U))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  if (state->phase == NPU_WIFI_RRO_TABLE_IDLE) {
    state->cursor = *cursor;
    state->retry_performed = false;
    state->phase = NPU_WIFI_RRO_TABLE_READ_INITIAL;
  } else if (!cursors_match(&state->cursor, cursor)) {
    set_result(state, result);
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  for (;;) {
    if (state->phase == NPU_WIFI_RRO_TABLE_CLAIMED) {
      set_result(state, result);
      return NPU_RUNTIME_SUCCESS;
    }
    if (state->phase == NPU_WIFI_RRO_TABLE_INVALIDATE_CLAIM) {
      set_result(state, result);
      return NPU_RUNTIME_OWNERSHIP_ERROR;
    }
    if (state->phase == NPU_WIFI_RRO_TABLE_INVALIDATE_MISMATCH)
      return invalidate_mismatch(state, operations, context, result);
    if (state->phase == NPU_WIFI_RRO_TABLE_DELAY_RETRY) {
      status = operations->delay_retry(context);
      if (status != NPU_RUNTIME_SUCCESS)
        return status;
      state->retry_performed = true;
      state->phase = NPU_WIFI_RRO_TABLE_READ_RETRY;
      continue;
    }

    status = read_entry(state, operations, context);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    if (state->entry.state == NPU_WIFI_RRO_METADATA_ENTRY_MATCHED) {
      state->phase = NPU_WIFI_RRO_TABLE_CLAIMED;
      set_result(state, result);
      return NPU_RUNTIME_SUCCESS;
    }
    if (state->phase == NPU_WIFI_RRO_TABLE_READ_INITIAL &&
        state->entry.state == NPU_WIFI_RRO_METADATA_ENTRY_RETRY_SENTINEL) {
      state->phase = NPU_WIFI_RRO_TABLE_DELAY_RETRY;
      continue;
    }

    state->phase = NPU_WIFI_RRO_TABLE_INVALIDATE_MISMATCH;
  }
}

enum npu_runtime_result npu_wifi_rro_table_complete(
    struct npu_wifi_rro_table_state *state,
    const struct npu_wifi_rro_table_operations *operations, void *context) {
  enum npu_runtime_result status;

  if (state == NULL || operations == NULL || operations->invalidate == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (state->phase != NPU_WIFI_RRO_TABLE_CLAIMED &&
      state->phase != NPU_WIFI_RRO_TABLE_INVALIDATE_CLAIM)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  state->phase = NPU_WIFI_RRO_TABLE_INVALIDATE_CLAIM;
  status = operations->invalidate(context, &state->cursor);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  state->phase = NPU_WIFI_RRO_TABLE_IDLE;
  return NPU_RUNTIME_SUCCESS;
}

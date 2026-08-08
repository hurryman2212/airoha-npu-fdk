/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_fragment.h"

#include "an7581/runtime/memory.h"

#define NPU_WIFI_RRO_FINAL_FRAGMENT_FLAG UINT32_C(2)
#define NPU_WIFI_RRO_FRAGMENT_COUNT_SHIFT UINT32_C(5)
#define NPU_WIFI_RRO_FRAGMENT_ORDINAL_SHIFT UINT32_C(2)
#define NPU_WIFI_RRO_FRAGMENT_FLAGS_MASK UINT32_C(0xfe)
#define NPU_WIFI_RRO_TOTAL_LENGTH_SHIFT UINT32_C(3)
#define NPU_WIFI_RRO_TOTAL_LENGTH_MASK UINT32_C(0x3fff)
#define NPU_WIFI_RRO_PRESERVE_MASK UINT32_C(0xfffe0007)

void npu_wifi_rro_fragment_initialize(
    struct npu_wifi_rro_fragment_state *state) {
  if (state != NULL)
    (void)npu_memset(state, 0U, sizeof(*state));
}

static bool
records_match(const struct npu_wifi_rro_metadata_record_fields *left,
              const struct npu_wifi_rro_metadata_record_fields *right) {
  return left->packet_control == right->packet_control &&
         left->buffer_id == right->buffer_id &&
         left->signed_buffer_id == right->signed_buffer_id &&
         left->packet_length == right->packet_length &&
         left->last_segment == right->last_segment;
}

static void stage_write_control(struct npu_wifi_rro_fragment_state *state,
                                uint16_t buffer_id, uint32_t packet_control) {
  struct npu_wifi_rro_fragment_action *action =
      &state->actions[state->action_count];

  action->type = NPU_WIFI_RRO_FRAGMENT_WRITE_CONTROL;
  action->buffer_id = buffer_id;
  action->packet_control = packet_control;
  ++state->action_count;
}

static void stage_dispatch(struct npu_wifi_rro_fragment_state *state,
                           uint16_t buffer_id, uint16_t total_length,
                           uint8_t flags, uint16_t fragment_length) {
  struct npu_wifi_rro_fragment_action *action =
      &state->actions[state->action_count];

  action->type = NPU_WIFI_RRO_FRAGMENT_DISPATCH;
  action->buffer_id = buffer_id;
  action->total_length = total_length;
  action->fragment_length = fragment_length;
  action->flags = flags;
  ++state->action_count;
}

static void stage_release(struct npu_wifi_rro_fragment_state *state,
                          uint16_t buffer_id) {
  struct npu_wifi_rro_fragment_action *action =
      &state->actions[state->action_count];

  action->type = NPU_WIFI_RRO_FRAGMENT_RELEASE;
  action->buffer_id = buffer_id;
  ++state->action_count;
}

static uint8_t fragment_dispatch_flags(uint8_t fragment_count,
                                       uint8_t fragment_index) {
  uint32_t flags;

  flags =
      ((uint32_t)fragment_count << NPU_WIFI_RRO_FRAGMENT_COUNT_SHIFT) |
      ((uint32_t)(fragment_index + 1U) << NPU_WIFI_RRO_FRAGMENT_ORDINAL_SHIFT);
  if (fragment_index + 1U == fragment_count)
    flags |= NPU_WIFI_RRO_FINAL_FRAGMENT_FLAG;
  return (uint8_t)(flags & NPU_WIFI_RRO_FRAGMENT_FLAGS_MASK);
}

static void clear_active_chain(struct npu_wifi_rro_fragment_state *state) {
  state->first_packet_control = 0U;
  state->total_length = 0U;
  state->fragment_count = 0U;
  state->active = false;
  state->discarding = false;
}

static void
append_fragment(struct npu_wifi_rro_fragment_state *state,
                const struct npu_wifi_rro_metadata_record_fields *record) {
  struct npu_wifi_rro_fragment *fragment =
      &state->fragments[state->fragment_count];

  fragment->buffer_id = record->buffer_id;
  fragment->length = record->packet_length;
  state->total_length += record->packet_length;
  ++state->fragment_count;
}

static void stage_completed_chain(struct npu_wifi_rro_fragment_state *state) {
  uint32_t packet_control;
  uint8_t fragment_count = state->fragment_count;
  uint8_t fragment_index;
  uint16_t total_length = (uint16_t)state->total_length;

  packet_control = (state->first_packet_control & NPU_WIFI_RRO_PRESERVE_MASK) |
                   ((state->total_length & NPU_WIFI_RRO_TOTAL_LENGTH_MASK)
                    << NPU_WIFI_RRO_TOTAL_LENGTH_SHIFT);
  stage_write_control(state, state->fragments[0].buffer_id, packet_control);
  for (fragment_index = 0U; fragment_index < fragment_count; ++fragment_index) {
    stage_dispatch(state, state->fragments[fragment_index].buffer_id,
                   total_length,
                   fragment_dispatch_flags(fragment_count, fragment_index),
                   state->fragments[fragment_index].length);
  }
  clear_active_chain(state);
}

static void
stage_new_record(struct npu_wifi_rro_fragment_state *state,
                 const struct npu_wifi_rro_metadata_record_fields *record) {
  bool continuation = (record->packet_control & 1U) != 0U;
  uint8_t fragment_index;

  state->action_count = 0U;
  state->action_index = 0U;
  if (state->discarding) {
    stage_release(state, record->buffer_id);
    if (!continuation)
      clear_active_chain(state);
    return;
  }

  if (!state->active) {
    if (!continuation) {
      stage_write_control(state, record->buffer_id, record->packet_control);
      stage_dispatch(state, record->buffer_id, record->packet_length,
                     (uint8_t)NPU_WIFI_RRO_FINAL_FRAGMENT_FLAG,
                     record->packet_length);
      return;
    }

    state->active = true;
    state->first_packet_control = record->packet_control;
    state->total_length = 0U;
    state->fragment_count = 0U;
    stage_write_control(state, record->buffer_id, record->packet_control);
  }

  append_fragment(state, record);
  if (!continuation) {
    stage_completed_chain(state);
    return;
  }
  if (state->fragment_count <= 6U)
    return;

  for (fragment_index = 0U; fragment_index < state->fragment_count;
       ++fragment_index)
    stage_release(state, state->fragments[fragment_index].buffer_id);
  state->first_packet_control = 0U;
  state->total_length = 0U;
  state->fragment_count = 0U;
  state->discarding = true;
}

static enum npu_runtime_result
execute_actions(struct npu_wifi_rro_fragment_state *state,
                const struct npu_wifi_rro_fragment_operations *operations,
                void *context, struct npu_wifi_rro_fragment_result *result) {
  while (state->action_index < state->action_count) {
    struct npu_wifi_rro_fragment_action *action =
        &state->actions[state->action_index];
    enum npu_runtime_result status;

    if (action->type == NPU_WIFI_RRO_FRAGMENT_WRITE_CONTROL) {
      status = operations->write_control(context, action->buffer_id,
                                         action->packet_control);
    } else if (action->type == NPU_WIFI_RRO_FRAGMENT_DISPATCH) {
      status = operations->dispatch(context, (int16_t)action->buffer_id,
                                    action->total_length, action->flags,
                                    action->fragment_length);
    } else {
      status = operations->release(context, action->buffer_id);
    }
    if (status == NPU_RUNTIME_REJECTED &&
        action->type == NPU_WIFI_RRO_FRAGMENT_DISPATCH) {
      action->type = NPU_WIFI_RRO_FRAGMENT_RELEASE;
      ++state->rejected_dispatch_count;
      ++result->dispatch_rejections;
      continue;
    }
    if (status != NPU_RUNTIME_SUCCESS)
      return status;

    ++state->action_index;
    ++result->actions_completed;
  }

  state->action_count = 0U;
  state->action_index = 0U;
  state->record_pending = false;
  result->record_committed = true;
  return NPU_RUNTIME_SUCCESS;
}

static void set_result_state(const struct npu_wifi_rro_fragment_state *state,
                             struct npu_wifi_rro_fragment_result *result) {
  result->fragment_count = state->fragment_count;
  result->active = state->active;
  result->discarding = state->discarding;
}

enum npu_runtime_result npu_wifi_rro_fragment_consume(
    struct npu_wifi_rro_fragment_state *state,
    const struct npu_wifi_rro_metadata_record_fields *record,
    const struct npu_wifi_rro_fragment_operations *operations, void *context,
    struct npu_wifi_rro_fragment_result *result) {
  enum npu_runtime_result status;

  if (state == NULL || record == NULL || operations == NULL ||
      operations->write_control == NULL || operations->dispatch == NULL ||
      operations->release == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (record->packet_length > NPU_WIFI_RRO_TOTAL_LENGTH_MASK ||
      state->fragment_count > NPU_WIFI_RRO_FRAGMENT_LIMIT ||
      state->action_count > NPU_WIFI_RRO_FRAGMENT_ACTION_LIMIT ||
      state->action_index > state->action_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (state->record_pending) {
    if (!records_match(&state->pending_record, record)) {
      set_result_state(state, result);
      return NPU_RUNTIME_OWNERSHIP_ERROR;
    }
  } else {
    state->pending_record = *record;
    state->record_pending = true;
    stage_new_record(state, record);
  }

  status = execute_actions(state, operations, context, result);
  set_result_state(state, result);
  return status;
}

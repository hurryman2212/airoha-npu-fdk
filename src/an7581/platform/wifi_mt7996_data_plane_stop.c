/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_data_plane_stop.h"

#include "an7581/runtime/memory.h"

enum component_index {
  COMPONENT_RRO_CONTROL = 0,
  COMPONENT_RX_REFILL,
  COMPONENT_COMPLETION,
  COMPONENT_TX_FAST_PATH,
  COMPONENT_PPE_RESULT,
  COMPONENT_COUNT,
};

static const uint32_t component_masks[COMPONENT_COUNT] = {
    AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL,
    AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL,
    AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION,
    AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH,
    AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT,
};

static void
result_update(const struct an7581_wifi_mt7996_data_plane_stop *stop,
              struct an7581_wifi_mt7996_data_plane_stop_result *result,
              enum npu_runtime_result status) {
  result->status = status;
  result->activation_mask = stop->activation_mask;
  result->prepare_required_mask = stop->prepare_required_mask;
  result->prepared_mask = stop->prepared_mask;
  result->deactivated_mask = stop->deactivated_mask;
  result->lane_mask = stop->lane_mask;
  result->quiesce_requested_mask = stop->quiesce_requested_mask;
  result->wake_completed_mask = stop->wake_completed_mask;
  result->quiesced_mask = stop->quiesced_mask;
  result->unpublished_mask = stop->unpublished_mask;
  result->resumed_lane_mask = stop->resumed_lane_mask;
  result->restart_active_mask = stop->restart_active_mask;
  result->board_resumed_mask = stop->board_resumed_mask;
  result->stopped = stop->stopped;
  result->restarted = stop->restarted;
}

static void
operation_copy(struct an7581_wifi_mt7996_data_plane_board_operation *operation,
               an7581_board_prepare_stop prepare_stop,
               an7581_board_resume resume, void *context) {
  operation->prepare_stop = prepare_stop;
  operation->resume = resume;
  operation->context = context;
}

static bool config_is_valid(
    const struct an7581_wifi_mt7996_data_plane_stop_config *config) {
  if (config == NULL || config->rro_control == NULL ||
      config->rx_refill == NULL || config->completion == NULL ||
      config->tx_fast_path == NULL || config->ppe_result == NULL ||
      config->completion_platform == NULL ||
      config->tx_fast_path_platform == NULL || config->core2_dispatch == NULL ||
      config->core56_dispatch == NULL || config->completion_dispatch == NULL ||
      config->rro_control_board == NULL || config->rx_refill_board == NULL ||
      config->completion_board == NULL || config->tx_fast_path_board == NULL ||
      config->ppe_result_board == NULL)
    return false;
  if (!config->rro_control->initialized || !config->rx_refill->initialized ||
      !config->completion->initialized || !config->tx_fast_path->initialized ||
      !config->ppe_result->initialized || config->rx_refill->dispatch == NULL ||
      !config->rx_refill->dispatch->initialized ||
      !config->core2_dispatch->initialized)
    return false;
  if (config->rro_control->activation_allowed !=
          config->rro_control_board->activation_allowed ||
      config->rx_refill->activation_allowed !=
          config->rx_refill_board->activation_allowed ||
      config->completion->activation_allowed !=
          config->completion_board->activation_allowed ||
      config->tx_fast_path->activation_allowed !=
          config->tx_fast_path_board->activation_allowed ||
      config->ppe_result->activation_allowed !=
          config->ppe_result_board->activation_allowed)
    return false;
  if (config->rx_refill->activation_allowed &&
      !config->rro_control->activation_allowed)
    return false;
  if ((config->tx_fast_path->activation_allowed ||
       config->ppe_result->activation_allowed) &&
      !config->completion->activation_allowed)
    return false;
  if (config->completion->activation_allowed &&
      (!config->completion_platform->initialized ||
       config->completion->operation_context != config->completion_platform ||
       config->completion_platform->dispatch != config->completion_dispatch))
    return false;
  if (config->tx_fast_path->activation_allowed &&
      (!config->tx_fast_path_platform->initialized ||
       config->tx_fast_path->operation_context !=
           config->tx_fast_path_platform ||
       config->tx_fast_path_platform->dispatch != config->core2_dispatch))
    return false;
  if (config->rro_control->activation_allowed &&
      (config->rro_control->platform == NULL ||
       !config->rro_control->platform->initialized ||
       config->rro_control->platform->dispatch != config->core56_dispatch))
    return false;
  return true;
}

enum npu_runtime_result an7581_wifi_mt7996_data_plane_stop_initialize(
    struct an7581_wifi_mt7996_data_plane_stop *stop,
    const struct an7581_wifi_mt7996_data_plane_stop_config *config) {
  if (stop == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (stop->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!config_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(stop, 0U, sizeof(*stop));
  stop->rro_control = config->rro_control;
  stop->rx_refill = config->rx_refill;
  stop->completion = config->completion;
  stop->tx_fast_path = config->tx_fast_path;
  stop->ppe_result = config->ppe_result;
  stop->completion_platform = config->completion_platform;
  stop->tx_fast_path_platform = config->tx_fast_path_platform;
  stop->core1_dispatch = config->rx_refill->dispatch;
  stop->core2_dispatch = config->core2_dispatch;
  stop->core56_dispatch = config->core56_dispatch;
  stop->completion_dispatch = config->completion_dispatch;
  operation_copy(&stop->operations[COMPONENT_RRO_CONTROL],
                 config->rro_control_board->prepare_stop,
                 config->rro_control_board->resume,
                 config->rro_control_board->stop_context);
  operation_copy(&stop->operations[COMPONENT_RX_REFILL],
                 config->rx_refill_board->prepare_stop,
                 config->rx_refill_board->resume,
                 config->rx_refill_board->stop_context);
  operation_copy(&stop->operations[COMPONENT_COMPLETION],
                 config->completion_board->prepare_stop,
                 config->completion_board->resume,
                 config->completion_board->stop_context);
  operation_copy(&stop->operations[COMPONENT_TX_FAST_PATH],
                 config->tx_fast_path_board->prepare_stop,
                 config->tx_fast_path_board->resume,
                 config->tx_fast_path_board->stop_context);
  operation_copy(&stop->operations[COMPONENT_PPE_RESULT],
                 config->ppe_result_board->prepare_stop,
                 config->ppe_result_board->resume,
                 config->ppe_result_board->stop_context);
  stop->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t
activation_mask(const struct an7581_wifi_mt7996_data_plane_stop *stop) {
  uint32_t mask = 0U;

  if (stop->rro_control->activation_allowed)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL;
  if (stop->rx_refill->activation_allowed)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL;
  if (stop->completion->activation_allowed)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION;
  if (stop->tx_fast_path->activation_allowed)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH;
  if (stop->ppe_result->activation_allowed)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT;
  return mask;
}

static uint32_t
prepare_required_mask(const struct an7581_wifi_mt7996_data_plane_stop *stop) {
  uint32_t mask = 0U;

  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL) !=
          0U &&
      (stop->rro_control->pipeline_initialized ||
       stop->rro_control->backends_bound ||
       stop->rro_control->control_plane_initialized ||
       stop->rro_control->runtime_published))
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL) != 0U &&
      (stop->rx_refill->rings_bound || stop->rx_refill->worker_published))
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION) !=
          0U &&
      (stop->completion->shared_state_initialized ||
       stop->completion->pipeline_initialized ||
       stop->completion->pipeline_published))
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH) !=
          0U &&
      (stop->tx_fast_path->fast_path_initialized ||
       stop->tx_fast_path->fast_path_published))
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) !=
          0U &&
      stop->ppe_result->bundle.initialized)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT;
  return mask;
}

static uint32_t
lane_mask(const struct an7581_wifi_mt7996_data_plane_stop *stop) {
  uint32_t mask = 0U;

  if (stop->rx_refill->worker_published)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE1;
  if (stop->tx_fast_path->fast_path_published)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE2;
  if (stop->completion->pipeline_published)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS;
  if (stop->rro_control->runtime_published)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE56;
  return mask;
}

static void
set_activation_allowed(struct an7581_wifi_mt7996_data_plane_stop *stop,
                       bool allowed) {
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL) != 0U)
    stop->rro_control->activation_allowed = allowed;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL) != 0U)
    stop->rx_refill->activation_allowed = allowed;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION) != 0U)
    stop->completion->activation_allowed = allowed;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH) !=
      0U)
    stop->tx_fast_path->activation_allowed = allowed;
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) != 0U)
    stop->ppe_result->activation_allowed = allowed;
}

static enum npu_runtime_result
stop_begin(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  size_t index;

  stop->activation_mask = activation_mask(stop);
  stop->prepare_required_mask = prepare_required_mask(stop);
  stop->lane_mask = lane_mask(stop);
  for (index = 0U; index < COMPONENT_COUNT; ++index) {
    if ((stop->prepare_required_mask & component_masks[index]) != 0U &&
        (stop->operations[index].prepare_stop == NULL ||
         stop->operations[index].resume == NULL))
      return NPU_RUNTIME_OUT_OF_RANGE;
  }

  set_activation_allowed(stop, false);
  stop->stop_started = true;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
prepare_components(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  static const enum component_index prepare_order[] = {
      COMPONENT_RX_REFILL,   COMPONENT_TX_FAST_PATH, COMPONENT_PPE_RESULT,
      COMPONENT_RRO_CONTROL, COMPONENT_COMPLETION,
  };
  size_t order_index;

  for (order_index = 0U;
       order_index < sizeof(prepare_order) / sizeof(prepare_order[0]);
       ++order_index) {
    const enum component_index index = prepare_order[order_index];
    const uint32_t component = component_masks[index];
    enum npu_runtime_result status;

    if ((stop->prepare_required_mask & component) == 0U ||
        (stop->prepared_mask & component) != 0U)
      continue;
    ++stop->prepare_attempt_count[index];
    status =
        stop->operations[index].prepare_stop(stop->operations[index].context);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->prepared_mask |= component;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
deactivate_ppe_result(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  enum npu_runtime_result status;

  if ((stop->prepare_required_mask &
       AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) == 0U ||
      (stop->deactivated_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) != 0U)
    return NPU_RUNTIME_SUCCESS;
  status = an7581_wifi_mt7996_ppe_result_bundle_set_active(
      &stop->ppe_result->bundle, false);
  if (status == NPU_RUNTIME_SUCCESS)
    stop->deactivated_mask |= AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT;
  return status;
}

static enum npu_runtime_result
request_quiesce(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  struct an7581_wifi_mt7996_completion_platform *completion;
  struct an7581_wifi_mt7996_tx_fast_path_platform *tx_fast_path;
  enum npu_runtime_result status;

  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) != 0U &&
      (stop->quiesce_requested_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) ==
          0U) {
    status = an7581_core1_dispatch_request_quiesce(stop->core1_dispatch,
                                                   stop->rx_refill);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->quiesce_requested_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE1;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) != 0U &&
      (stop->quiesce_requested_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) ==
          0U) {
    tx_fast_path = stop->tx_fast_path_platform;
    if (tx_fast_path == NULL || !tx_fast_path->fast_path.initialized)
      return NPU_RUNTIME_OUT_OF_RANGE;
    status = an7581_core2_dispatch_request_quiesce(
        stop->core2_dispatch, &tx_fast_path->fast_path.runtime);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->quiesce_requested_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE2;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) !=
          0U &&
      (stop->quiesce_requested_mask &
       AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) == 0U) {
    completion = stop->completion_platform;
    if (completion == NULL || !completion->pipeline.initialized)
      return NPU_RUNTIME_OUT_OF_RANGE;
    status = an7581_wifi_mt7996_completion_dispatch_request_quiesce(
        stop->completion_dispatch, &completion->pipeline);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->quiesce_requested_mask |=
        AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) != 0U &&
      (stop->quiesce_requested_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) ==
          0U) {
    status = an7581_core56_dispatch_request_quiesce(
        stop->core56_dispatch, &stop->rro_control->platform->pipeline.runtime);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->quiesce_requested_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE56;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
wake_workers(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  enum npu_runtime_result status;

  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) != 0U &&
      (stop->wake_completed_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) == 0U) {
    ++stop->wake_attempt_count[0];
    status = stop->rx_refill->operations->wake_worker(
        stop->rx_refill->operation_context, AN7581_CORE1_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->wake_completed_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE1;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) != 0U &&
      (stop->wake_completed_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) == 0U) {
    ++stop->wake_attempt_count[1];
    status = stop->tx_fast_path->operations->wake_worker(
        stop->tx_fast_path->operation_context, AN7581_CORE2_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->wake_completed_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE2;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) !=
          0U &&
      (stop->wake_completed_mask &
       AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) == 0U) {
    ++stop->wake_attempt_count[2];
    status = stop->completion->operations->wake_workers(
        stop->completion->operation_context,
        AN7581_WIFI_MT7996_COMPLETION_WORKER_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->wake_completed_mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) != 0U &&
      (stop->wake_completed_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) ==
          0U) {
    ++stop->wake_attempt_count[3];
    status = stop->rro_control->platform->wake_workers(
        stop->rro_control->platform->wake_context,
        AN7581_WIFI_MT7996_RRO_CONTROL_WORKER_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->wake_completed_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE56;
  }
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t
quiesced_lanes(const struct an7581_wifi_mt7996_data_plane_stop *stop) {
  uint32_t mask = 0U;

  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) != 0U &&
      stop->core1_dispatch->quiesced)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE1;
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) != 0U &&
      stop->core2_dispatch->quiesced)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE2;
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) !=
          0U &&
      stop->completion_dispatch->packet_queue_quiesced &&
      stop->completion_dispatch->tx_done_quiesced)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS;
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) != 0U &&
      stop->core56_dispatch->core5_quiesced &&
      stop->core56_dispatch->core6_quiesced)
    mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE56;
  return mask;
}

static enum npu_runtime_result
unpublish_lanes(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  struct an7581_wifi_mt7996_completion_platform *completion;
  struct an7581_wifi_mt7996_tx_fast_path_platform *tx_fast_path;
  enum npu_runtime_result status;

  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) != 0U &&
      (stop->unpublished_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) == 0U) {
    status =
        an7581_core1_dispatch_unpublish(stop->core1_dispatch, stop->rx_refill);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->unpublished_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE1;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) != 0U &&
      (stop->unpublished_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) == 0U) {
    tx_fast_path = stop->tx_fast_path_platform;
    status = an7581_core2_dispatch_unpublish(stop->core2_dispatch,
                                             &tx_fast_path->fast_path.runtime);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->unpublished_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE2;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) !=
          0U &&
      (stop->unpublished_mask &
       AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) == 0U) {
    completion = stop->completion_platform;
    status = an7581_wifi_mt7996_completion_dispatch_unpublish(
        stop->completion_dispatch, &completion->pipeline);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->unpublished_mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) != 0U &&
      (stop->unpublished_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) == 0U) {
    status = an7581_core56_dispatch_unpublish(
        stop->core56_dispatch, &stop->rro_control->platform->pipeline.runtime);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->unpublished_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE56;
  }
  return NPU_RUNTIME_SUCCESS;
}

static void
suspend_lifecycles(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL) !=
      0U) {
    stop->rro_control->runtime_published = false;
    stop->rro_control->workers_woken = false;
    stop->rro_control->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_ACTIVATION_GATED;
    stop->rro_control->last_status = NPU_RUNTIME_REJECTED;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL) != 0U) {
    stop->rx_refill->worker_published = false;
    stop->rx_refill->worker_woken = false;
    stop->rx_refill->state =
        AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_ACTIVATION_GATED;
    stop->rx_refill->last_status = NPU_RUNTIME_REJECTED;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION) !=
      0U) {
    stop->completion->pipeline_published = false;
    stop->completion->workers_woken = false;
    stop->completion->state =
        AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_ACTIVATION_GATED;
    stop->completion->last_status = NPU_RUNTIME_REJECTED;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH) !=
      0U) {
    stop->tx_fast_path->fast_path_published = false;
    stop->tx_fast_path->worker_woken = false;
    stop->tx_fast_path->state =
        AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_ACTIVATION_GATED;
    stop->tx_fast_path->last_status = NPU_RUNTIME_REJECTED;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) !=
      0U) {
    stop->ppe_result->state =
        AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_ACTIVATION_GATED;
    stop->ppe_result->last_status = NPU_RUNTIME_REJECTED;
  }
}

static enum npu_runtime_result
resume_lanes(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  enum npu_runtime_result status;

  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) != 0U &&
      (stop->resumed_lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE1) == 0U) {
    status = an7581_core1_dispatch_resume(stop->core1_dispatch);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->resumed_lane_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE1;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) != 0U &&
      (stop->resumed_lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE2) == 0U) {
    status = an7581_core2_dispatch_resume(stop->core2_dispatch);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->resumed_lane_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE2;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) !=
          0U &&
      (stop->resumed_lane_mask &
       AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS) == 0U) {
    status = an7581_wifi_mt7996_completion_dispatch_resume(
        stop->completion_dispatch);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->resumed_lane_mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS;
  }
  if ((stop->lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) != 0U &&
      (stop->resumed_lane_mask & AN7581_WIFI_MT7996_DATA_PLANE_CORE56) == 0U) {
    status = an7581_core56_dispatch_resume(stop->core56_dispatch);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->resumed_lane_mask |= AN7581_WIFI_MT7996_DATA_PLANE_CORE56;
  }
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_data_plane_stop_step(
    struct an7581_wifi_mt7996_data_plane_stop *stop,
    struct an7581_wifi_mt7996_data_plane_stop_result *result) {
  enum npu_runtime_result status;

  if (stop == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!stop->initialized)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (stop->restart_started)
    return NPU_RUNTIME_REJECTED;
  if (stop->stopped) {
    result_update(stop, result, NPU_RUNTIME_SUCCESS);
    return result->status;
  }

  if (!stop->stop_started) {
    status = stop_begin(stop);
    if (status != NPU_RUNTIME_SUCCESS) {
      result_update(stop, result, status);
      return status;
    }
  }
  status = prepare_components(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    result->waiting_for_owner = true;
    return status;
  }
  status = deactivate_ppe_result(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    result->waiting_for_owner = true;
    return status;
  }
  status = request_quiesce(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    return status;
  }
  status = wake_workers(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    result->waiting_for_wake = true;
    return status;
  }

  stop->quiesced_mask = quiesced_lanes(stop);
  if (stop->quiesced_mask != stop->lane_mask) {
    result_update(stop, result, NPU_RUNTIME_EMPTY);
    result->waiting_for_workers = true;
    return result->status;
  }
  status = unpublish_lanes(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    return status;
  }
  suspend_lifecycles(stop);
  status = resume_lanes(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    return status;
  }

  stop->stopped = true;
  result_update(stop, result, NPU_RUNTIME_SUCCESS);
  return result->status;
}

static void restart_begin(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  set_activation_allowed(stop, true);
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL) !=
      0U) {
    stop->rro_control->state =
        AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    stop->rro_control->last_status = NPU_RUNTIME_EMPTY;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL) != 0U) {
    stop->rx_refill->state =
        AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    stop->rx_refill->last_status = NPU_RUNTIME_EMPTY;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION) !=
      0U) {
    stop->completion->state =
        AN7581_WIFI_MT7996_COMPLETION_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    stop->completion->last_status = NPU_RUNTIME_EMPTY;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH) !=
      0U) {
    stop->tx_fast_path->state =
        AN7581_WIFI_MT7996_TX_FAST_PATH_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    stop->tx_fast_path->last_status = NPU_RUNTIME_EMPTY;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) !=
      0U) {
    stop->ppe_result->state =
        AN7581_WIFI_MT7996_PPE_RESULT_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    stop->ppe_result->last_status = NPU_RUNTIME_EMPTY;
  }
  stop->restart_started = true;
}

static enum npu_runtime_result
restart_lifecycles(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  struct an7581_wifi_mt7996_rro_control_lifecycle_result rro_result;
  struct an7581_wifi_mt7996_rx_refill_lifecycle_result rx_result;
  struct an7581_wifi_mt7996_completion_lifecycle_result completion_result;
  struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result tx_result;
  struct an7581_wifi_mt7996_ppe_result_lifecycle_result ppe_result;
  enum npu_runtime_result status;

  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL) !=
          0U &&
      (stop->restart_active_mask & AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL) ==
          0U) {
    status = an7581_wifi_mt7996_rro_control_lifecycle_step(stop->rro_control,
                                                           &rro_result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->restart_active_mask |= AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION) !=
          0U &&
      (stop->restart_active_mask & AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION) ==
          0U) {
    status = an7581_wifi_mt7996_completion_lifecycle_step(stop->completion,
                                                          &completion_result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->restart_active_mask |= AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL) != 0U &&
      (stop->restart_active_mask & AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL) ==
          0U) {
    status = an7581_wifi_mt7996_rx_refill_lifecycle_step(stop->rx_refill,
                                                         &rx_result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->restart_active_mask |= AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH) !=
          0U &&
      (stop->restart_active_mask &
       AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH) == 0U) {
    status = an7581_wifi_mt7996_tx_fast_path_lifecycle_step(stop->tx_fast_path,
                                                            &tx_result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->restart_active_mask |= AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH;
  }
  if ((stop->activation_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) !=
          0U &&
      (stop->restart_active_mask & AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT) ==
          0U) {
    status = an7581_wifi_mt7996_ppe_result_lifecycle_step(stop->ppe_result,
                                                          &ppe_result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->restart_active_mask |= AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT;
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
resume_board_components(struct an7581_wifi_mt7996_data_plane_stop *stop) {
  static const enum component_index resume_order[] = {
      COMPONENT_COMPLETION,   COMPONENT_RRO_CONTROL, COMPONENT_PPE_RESULT,
      COMPONENT_TX_FAST_PATH, COMPONENT_RX_REFILL,
  };
  size_t order_index;

  for (order_index = 0U;
       order_index < sizeof(resume_order) / sizeof(resume_order[0]);
       ++order_index) {
    const enum component_index index = resume_order[order_index];
    const uint32_t component = component_masks[index];
    enum npu_runtime_result status;

    if ((stop->prepared_mask & component) == 0U ||
        (stop->board_resumed_mask & component) != 0U)
      continue;
    ++stop->board_resume_attempt_count[index];
    status = stop->operations[index].resume(stop->operations[index].context);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    stop->board_resumed_mask |= component;
  }
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_data_plane_restart_step(
    struct an7581_wifi_mt7996_data_plane_stop *stop,
    struct an7581_wifi_mt7996_data_plane_stop_result *result) {
  enum npu_runtime_result status;

  if (stop == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!stop->initialized)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (!stop->stopped)
    return NPU_RUNTIME_REJECTED;
  if (stop->restarted) {
    result_update(stop, result, NPU_RUNTIME_SUCCESS);
    return result->status;
  }

  if (!stop->restart_started)
    restart_begin(stop);
  status = restart_lifecycles(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    result->waiting_for_lifecycle = true;
    return status;
  }
  status = resume_board_components(stop);
  if (status != NPU_RUNTIME_SUCCESS) {
    result_update(stop, result, status);
    result->waiting_for_board_resume = true;
    return status;
  }

  stop->restarted = true;
  result_update(stop, result, NPU_RUNTIME_SUCCESS);
  return result->status;
}

enum npu_runtime_result an7581_wifi_mt7996_data_plane_stop_reset(
    struct an7581_wifi_mt7996_data_plane_stop *stop) {
  uint32_t index;

  if (stop == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!stop->initialized)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (!stop->restarted)
    return NPU_RUNTIME_REJECTED;

  stop->activation_mask = 0U;
  stop->prepare_required_mask = 0U;
  stop->prepared_mask = 0U;
  stop->deactivated_mask = 0U;
  stop->lane_mask = 0U;
  stop->quiesce_requested_mask = 0U;
  stop->wake_completed_mask = 0U;
  stop->quiesced_mask = 0U;
  stop->unpublished_mask = 0U;
  stop->resumed_lane_mask = 0U;
  stop->restart_active_mask = 0U;
  stop->board_resumed_mask = 0U;
  for (index = 0U; index < COMPONENT_COUNT; ++index) {
    stop->prepare_attempt_count[index] = 0U;
    stop->board_resume_attempt_count[index] = 0U;
  }
  for (index = 0U; index < sizeof(stop->wake_attempt_count) /
                               sizeof(stop->wake_attempt_count[0]);
       ++index)
    stop->wake_attempt_count[index] = 0U;
  stop->stop_started = false;
  stop->stopped = false;
  stop->restart_started = false;
  stop->restarted = false;
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_completion_dispatch.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool pipeline_is_ready(
    const struct an7581_wifi_mt7996_completion_pipeline *pipeline) {
  return pipeline != NULL && pipeline->initialized &&
         pipeline->tx_done.initialized &&
         pipeline->tx_done.service.initialized &&
         pipeline->packet_consumers[0].initialized &&
         pipeline->packet_consumers[0].service.initialized &&
         pipeline->packet_consumers[1].initialized &&
         pipeline->packet_consumers[1].service.initialized &&
         pipeline->fragment_consumer.initialized &&
         pipeline->host_tx_consumer.initialized &&
         pipeline->runtime.initialized &&
         pipeline->runtime.service.initialized &&
         pipeline->runtime.service.tx_done == &pipeline->tx_done.service &&
         pipeline->runtime.service.packet_consumers[0] ==
             &pipeline->packet_consumers[0].service &&
         pipeline->runtime.service.packet_consumers[1] ==
             &pipeline->packet_consumers[1].service &&
         pipeline->runtime.service.fragment_consumer ==
             &pipeline->fragment_consumer &&
         pipeline->runtime.service.host_tx_consumer ==
             &pipeline->host_tx_consumer;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_publish(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch,
    struct an7581_wifi_mt7996_completion_pipeline *pipeline) {
  if (dispatch == NULL || pipeline == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (dispatch->pipeline != NULL || dispatch->quiesce_requested ||
      dispatch->packet_queue_quiesced || dispatch->tx_done_quiesced)
    return NPU_RUNTIME_REJECTED;
  if (!pipeline_is_ready(pipeline))
    return NPU_RUNTIME_OUT_OF_RANGE;

  an7581_dma_memory_barrier();
  dispatch->pipeline = pipeline;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_request_quiesce(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch,
    struct an7581_wifi_mt7996_completion_pipeline *pipeline) {
  if (dispatch == NULL || pipeline == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (dispatch->pipeline != pipeline)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (dispatch->quiesce_requested)
    return NPU_RUNTIME_SUCCESS;
  if (dispatch->packet_queue_quiesced || dispatch->tx_done_quiesced)
    return NPU_RUNTIME_OUT_OF_RANGE;

  dispatch->packet_queue_quiesced = false;
  dispatch->tx_done_quiesced = false;
  an7581_dma_memory_barrier();
  dispatch->quiesce_requested = true;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_unpublish(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch,
    struct an7581_wifi_mt7996_completion_pipeline *pipeline) {
  if (dispatch == NULL || pipeline == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->quiesce_requested || !dispatch->packet_queue_quiesced ||
      !dispatch->tx_done_quiesced)
    return NPU_RUNTIME_EMPTY;
  if (dispatch->pipeline != pipeline)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->pipeline = NULL;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_resume(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch) {
  if (dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!dispatch->quiesce_requested || !dispatch->packet_queue_quiesced ||
      !dispatch->tx_done_quiesced)
    return NPU_RUNTIME_REJECTED;
  if (dispatch->pipeline != NULL)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  dispatch->quiesce_requested = false;
  an7581_dma_memory_barrier();
  dispatch->packet_queue_quiesced = false;
  dispatch->tx_done_quiesced = false;
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_step(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch, uint32_t hart_id,
    struct an7581_wifi_mt7996_completion_dispatch_result *result) {
  struct an7581_wifi_mt7996_completion_pipeline *pipeline;

  if (dispatch == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (hart_id != AN7581_WIFI_MT7996_COMPLETION_PACKET_QUEUE_HART &&
      hart_id != AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART) {
    result->status = NPU_RUNTIME_REJECTED;
    result->should_backoff = true;
    return result->status;
  }

  result->quiesce_requested = dispatch->quiesce_requested;
  an7581_dma_memory_barrier();
  if (result->quiesce_requested) {
    if (hart_id == AN7581_WIFI_MT7996_COMPLETION_PACKET_QUEUE_HART)
      dispatch->packet_queue_quiesced = true;
    else
      dispatch->tx_done_quiesced = true;
    an7581_dma_memory_barrier();
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_pipeline = true;
    result->quiesced = true;
    result->should_backoff = true;
    return result->status;
  }
  if (dispatch->packet_queue_quiesced || dispatch->tx_done_quiesced) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  pipeline = dispatch->pipeline;
  an7581_dma_memory_barrier();
  if (pipeline == NULL) {
    result->status = NPU_RUNTIME_EMPTY;
    result->waiting_for_pipeline = true;
    result->should_backoff = true;
    return result->status;
  }
  if (!pipeline_is_ready(pipeline)) {
    result->status = NPU_RUNTIME_OUT_OF_RANGE;
    result->should_backoff = true;
    return result->status;
  }

  if (hart_id == AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART) {
    result->role = AN7581_WIFI_MT7996_COMPLETION_DISPATCH_TX_DONE;
    result->status = npu_wifi_mt7996_completion_runtime_step_tx_done(
        &pipeline->runtime.service, &result->service.tx_done);
    result->should_backoff = result->service.tx_done.should_backoff;
    return result->status;
  }

  result->role = AN7581_WIFI_MT7996_COMPLETION_DISPATCH_PACKET_QUEUES;
  result->status = npu_wifi_mt7996_completion_runtime_step_packet_queues(
      &pipeline->runtime.service, &result->service.packet_queues);
  result->should_backoff = result->service.packet_queues.should_backoff;
  return result->status;
}

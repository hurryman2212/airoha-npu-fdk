/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_completion_runtime.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool pointer_is_word_aligned(const volatile uint32_t *pointer) {
  return pointer != NULL &&
         ((uintptr_t)pointer & (sizeof(uint32_t) - 1U)) == 0U;
}

static bool readiness_is_valid(
    const struct npu_wifi_mt7996_completion_readiness *readiness) {
  return readiness != NULL &&
         pointer_is_word_aligned(readiness->offload_initialized) &&
         readiness->tx_done_enabled != NULL &&
         readiness->tx_configuration_state != NULL &&
         readiness->tx_done_activity != NULL &&
         pointer_is_word_aligned(readiness->rx_ring_enabled) &&
         readiness->rx_configuration_state != NULL &&
         readiness->host_rx_rings_ready != NULL;
}

static bool packet_queue_budgets_are_valid(const uint32_t *budgets) {
  return budgets[0] != 0U &&
         budgets[0] <= NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET &&
         budgets[1] == NPU_WIFI_MT7996_COMPLETION_SECONDARY_BUDGET;
}

static bool configuration_is_valid(
    const struct npu_wifi_mt7996_completion_runtime_config *config) {
  return config != NULL && config->tx_done != NULL &&
         config->tx_done->initialized && config->packet_consumers[0] != NULL &&
         config->packet_consumers[0]->initialized &&
         config->packet_consumers[1] != NULL &&
         config->packet_consumers[1]->initialized &&
         config->fragment_consumer != NULL &&
         config->fragment_consumer->initialized &&
         config->host_tx_consumer != NULL &&
         config->host_tx_consumer->initialized &&
         readiness_is_valid(&config->readiness) &&
         config->tx_done_budget != 0U &&
         config->tx_done_budget <= NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT &&
         packet_queue_budgets_are_valid(config->packet_queue_budgets);
}

static bool
runtime_is_valid(const struct npu_wifi_mt7996_completion_runtime *runtime) {
  return runtime != NULL && runtime->initialized && runtime->tx_done != NULL &&
         runtime->tx_done->initialized &&
         runtime->packet_consumers[0] != NULL &&
         runtime->packet_consumers[0]->initialized &&
         runtime->packet_consumers[1] != NULL &&
         runtime->packet_consumers[1]->initialized &&
         runtime->fragment_consumer != NULL &&
         runtime->fragment_consumer->initialized &&
         runtime->host_tx_consumer != NULL &&
         runtime->host_tx_consumer->initialized &&
         readiness_is_valid(&runtime->readiness) &&
         runtime->tx_done_budget != 0U &&
         runtime->tx_done_budget <= NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT &&
         packet_queue_budgets_are_valid(runtime->packet_queue_budgets);
}

enum npu_runtime_result npu_wifi_mt7996_completion_runtime_initialize(
    struct npu_wifi_mt7996_completion_runtime *runtime,
    const struct npu_wifi_mt7996_completion_runtime_config *config) {
  if (runtime == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (runtime->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(runtime, 0U, sizeof(*runtime));
  runtime->tx_done = config->tx_done;
  runtime->packet_consumers[0] = config->packet_consumers[0];
  runtime->packet_consumers[1] = config->packet_consumers[1];
  runtime->fragment_consumer = config->fragment_consumer;
  runtime->host_tx_consumer = config->host_tx_consumer;
  runtime->readiness = config->readiness;
  runtime->tx_done_budget = config->tx_done_budget;
  runtime->packet_queue_budgets[0] = config->packet_queue_budgets[0];
  runtime->packet_queue_budgets[1] = config->packet_queue_budgets[1];
  runtime->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static bool
tx_done_is_ready(const struct npu_wifi_mt7996_completion_runtime *runtime,
                 struct npu_wifi_mt7996_completion_wait_state *wait) {
  uint32_t offload_initialized;
  uint8_t configuration_state;
  uint8_t tx_done_enabled;

  an7581_dma_memory_barrier();
  offload_initialized = *runtime->readiness.offload_initialized;
  tx_done_enabled = *runtime->readiness.tx_done_enabled;
  configuration_state = *runtime->readiness.tx_configuration_state;
  an7581_dma_memory_barrier();

  wait->waiting_for_offload = offload_initialized == 0U;
  wait->waiting_for_tx_done = tx_done_enabled == 0U;
  wait->waiting_for_tx_configuration =
      configuration_state != NPU_WIFI_MT7996_COMPLETION_READY_STATE;
  return !wait->waiting_for_offload && !wait->waiting_for_tx_done &&
         !wait->waiting_for_tx_configuration;
}

enum npu_runtime_result npu_wifi_mt7996_completion_runtime_step_tx_done(
    struct npu_wifi_mt7996_completion_runtime *runtime,
    struct npu_wifi_mt7996_completion_tx_done_result *result) {
  enum npu_runtime_result status;

  if (result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!runtime_is_valid(runtime))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!tx_done_is_ready(runtime, &result->wait)) {
    result->idle = true;
    result->should_backoff = true;
    return NPU_RUNTIME_EMPTY;
  }

  *runtime->readiness.tx_done_activity = 0U;
  an7581_dma_memory_barrier();
  status = npu_wifi_mt7996_tx_done_process(
      runtime->tx_done, runtime->tx_done_budget, &result->service);
  ++runtime->tx_done_step_count;

  result->pending_work = result->service.processed == runtime->tx_done_budget &&
                         !result->service.stopped_on_hardware_owned &&
                         !result->service.stopped_on_allocation_failure;
  result->idle = status == NPU_RUNTIME_EMPTY && result->service.processed == 0U;
  result->should_backoff = status != NPU_RUNTIME_SUCCESS ||
                           result->service.enqueue_failed ||
                           result->service.stop_status != NPU_RUNTIME_SUCCESS ||
                           !result->pending_work;
  return status;
}

static bool packet_queues_are_ready(
    const struct npu_wifi_mt7996_completion_runtime *runtime,
    struct npu_wifi_mt7996_completion_wait_state *wait) {
  uint32_t offload_initialized;
  uint32_t rx_ring_enabled;
  uint8_t configuration_state;
  uint8_t host_rx_rings_ready;

  an7581_dma_memory_barrier();
  offload_initialized = *runtime->readiness.offload_initialized;
  rx_ring_enabled = *runtime->readiness.rx_ring_enabled;
  configuration_state = *runtime->readiness.rx_configuration_state;
  host_rx_rings_ready = *runtime->readiness.host_rx_rings_ready;
  an7581_dma_memory_barrier();

  wait->waiting_for_offload = offload_initialized == 0U;
  wait->waiting_for_rx_ring = rx_ring_enabled == 0U;
  wait->waiting_for_rx_configuration =
      configuration_state != NPU_WIFI_MT7996_COMPLETION_READY_STATE;
  wait->waiting_for_host_rx =
      host_rx_rings_ready != NPU_WIFI_MT7996_COMPLETION_HOST_RX_READY;
  return !wait->waiting_for_offload && !wait->waiting_for_rx_ring &&
         !wait->waiting_for_rx_configuration && !wait->waiting_for_host_rx;
}

static void
remember_failure(struct npu_wifi_mt7996_completion_packet_queue_result *result,
                 enum npu_runtime_result status) {
  if (status != NPU_RUNTIME_SUCCESS && status != NPU_RUNTIME_EMPTY &&
      result->first_failure == NPU_RUNTIME_SUCCESS)
    result->first_failure = status;
}

enum npu_runtime_result npu_wifi_mt7996_completion_runtime_step_packet_queues(
    struct npu_wifi_mt7996_completion_runtime *runtime,
    struct npu_wifi_mt7996_completion_packet_queue_result *result) {
  uint32_t band;

  if (result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  result->first_failure = NPU_RUNTIME_SUCCESS;
  if (!runtime_is_valid(runtime))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!packet_queues_are_ready(runtime, &result->wait)) {
    result->idle = true;
    result->should_backoff = true;
    return NPU_RUNTIME_EMPTY;
  }

  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    enum npu_runtime_result status = npu_wifi_mt7996_host_tx_ring_consume(
        runtime->host_tx_consumer, band, NPU_WIFI_MT7996_HOST_TX_BATCH_LIMIT,
        &result->host_tx[band]);

    remember_failure(result, status);
    result->host_tx_staged += result->host_tx[band].processed;
    result->pending_work |= result->host_tx[band].pending_work;
  }
  for (band = 0U; band < NPU_WIFI_MT7996_COMPLETION_BAND_COUNT; ++band) {
    enum npu_runtime_result status = npu_wifi_mt7996_packet_queue_consume(
        runtime->packet_consumers[band], runtime->packet_queue_budgets[band],
        &result->bands[band]);

    remember_failure(result, status);
    remember_failure(result, result->bands[band].first_failure);
    result->processed += result->bands[band].processed;
    result->forwarded += result->bands[band].forwarded;
    if (result->bands[band].processed == runtime->packet_queue_budgets[band] &&
        !result->bands[band].stopped_on_empty)
      result->pending_work = true;
  }
  {
    enum npu_runtime_result status = npu_wifi_mt7996_fragment_queue_consume(
        runtime->fragment_consumer, &result->fragment);

    remember_failure(result, status);
    remember_failure(result, result->fragment.first_failure);
    result->processed += result->fragment.collected;
    result->forwarded += result->fragment.forwarded;
    result->pending_work |= result->fragment.pending_work;
  }
  ++runtime->packet_queue_step_count;

  result->idle = result->processed == 0U && result->host_tx_staged == 0U &&
                 !result->pending_work;
  result->should_backoff =
      result->first_failure != NPU_RUNTIME_SUCCESS || !result->pending_work;
  if (result->first_failure != NPU_RUNTIME_SUCCESS)
    return result->first_failure;
  return result->processed == 0U && result->host_tx_staged == 0U
             ? NPU_RUNTIME_EMPTY
             : NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_completion_board_binding.h"

#include "an7581/runtime/memory.h"

static void closed_configuration_initialize(
    struct an7581_wifi_mt7996_completion_board_configuration *configuration) {
  *configuration = (struct an7581_wifi_mt7996_completion_board_configuration){
      .vdma_poll_limit = AN7581_WIFI_MT7996_COMPLETION_DEFAULT_VDMA_POLL_LIMIT,
      .tx_done_budget = NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT,
      .band0_budget = NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET,
  };
}

enum npu_runtime_result an7581_wifi_mt7996_completion_board_binding_resolve(
    const struct an7581_wifi_mt7996_completion_board_binding *binding,
    struct an7581_wifi_mt7996_completion_board_configuration *configuration) {
  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(configuration, 0U, sizeof(*configuration));
  closed_configuration_initialize(configuration);
  if (binding == NULL || !binding->activation_allowed)
    return NPU_RUNTIME_SUCCESS;
  if (binding->wake_workers == NULL || binding->prepare_stop == NULL ||
      binding->resume == NULL || binding->vdma_poll_limit == 0U ||
      binding->tx_done_budget == 0U ||
      binding->tx_done_budget > NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT ||
      binding->band0_budget == 0U ||
      binding->band0_budget > NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET ||
      (uint32_t)binding->packet_queue_producer >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)binding->packet_queue_consumers[0] >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)binding->packet_queue_consumers[1] >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *configuration = (struct an7581_wifi_mt7996_completion_board_configuration){
      .wake_workers = binding->wake_workers,
      .wake_context = binding->wake_context,
      .prepare_stop = binding->prepare_stop,
      .resume = binding->resume,
      .stop_context = binding->stop_context,
      .vdma_poll_limit = binding->vdma_poll_limit,
      .tx_done_budget = binding->tx_done_budget,
      .band0_budget = binding->band0_budget,
      .packet_queue_producer = binding->packet_queue_producer,
      .packet_queue_consumers =
          {
              binding->packet_queue_consumers[0],
              binding->packet_queue_consumers[1],
          },
      .activation_allowed = true,
  };
  return NPU_RUNTIME_SUCCESS;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_ppe_result_board_binding.h"

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_board_binding_resolve(
    const struct an7581_wifi_mt7996_ppe_result_board_binding *binding,
    struct an7581_wifi_mt7996_ppe_result_board_configuration *configuration) {
  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *configuration = (struct an7581_wifi_mt7996_ppe_result_board_configuration){
      .hart_id = AN7581_WIFI_MT7996_PPE_RESULT_HART,
  };
  if (binding == NULL || !binding->activation_allowed)
    return NPU_RUNTIME_SUCCESS;
  if (binding->prepare_stop == NULL || binding->resume == NULL ||
      binding->hart_id >= AN7581_NPU_CORE_COUNT ||
      (uint32_t)binding->packet_queue_producer >=
          NPU_WIFI_MT7996_PACKET_QUEUE_ENTRY_COUNT ||
      (uint32_t)binding->fragment_queue_producer >=
          NPU_WIFI_MT7996_FRAGMENT_QUEUE_ENTRY_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *configuration = (struct an7581_wifi_mt7996_ppe_result_board_configuration){
      .memory = binding->memory,
      .hart_id = binding->hart_id,
      .packet_queue_producer = binding->packet_queue_producer,
      .fragment_queue_producer = binding->fragment_queue_producer,
      .prepare_stop = binding->prepare_stop,
      .resume = binding->resume,
      .stop_context = binding->stop_context,
      .activation_allowed = true,
  };
  return NPU_RUNTIME_SUCCESS;
}

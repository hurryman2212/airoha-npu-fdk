/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_rx_refill_board_binding.h"

enum npu_runtime_result an7581_wifi_mt7996_rx_refill_board_binding_resolve(
    const struct an7581_wifi_mt7996_rx_refill_board_binding *binding,
    struct an7581_wifi_mt7996_rx_refill_board_configuration *configuration) {
  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *configuration = (struct an7581_wifi_mt7996_rx_refill_board_configuration){0};
  if (binding == NULL || !binding->activation_allowed)
    return NPU_RUNTIME_SUCCESS;
  if (binding->operations == NULL || binding->operations->read32 == NULL ||
      binding->operations->write32 == NULL ||
      binding->operations->wake_worker == NULL ||
      binding->prepare_stop == NULL || binding->resume == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *configuration = (struct an7581_wifi_mt7996_rx_refill_board_configuration){
      .operations = binding->operations,
      .operation_context = binding->operation_context,
      .prepare_stop = binding->prepare_stop,
      .resume = binding->resume,
      .stop_context = binding->stop_context,
      .activation_allowed = true,
  };
  return NPU_RUNTIME_SUCCESS;
}

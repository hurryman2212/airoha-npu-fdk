/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tr471_board_binding.h"

#include "an7581/runtime/memory.h"

enum npu_runtime_result an7581_tr471_board_binding_resolve(
    const struct an7581_tr471_board_binding *binding,
    struct an7581_tr471_board_configuration *configuration) {
  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(configuration, 0U, sizeof(*configuration));
  if (binding == NULL || !binding->activation_allowed)
    return NPU_RUNTIME_SUCCESS;
  if (binding->wake_harts == NULL || binding->timer_clock_mhz == 0U ||
      binding->transmit_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT ||
      binding->receive_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (binding->shared_buffer_extent <
      NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *configuration = (struct an7581_tr471_board_configuration){
      .wake_harts = binding->wake_harts,
      .wake_context = binding->wake_context,
      .timer_clock_mhz = binding->timer_clock_mhz,
      .transmit_budget = binding->transmit_budget,
      .receive_budget = binding->receive_budget,
      .shared_buffer_extent = binding->shared_buffer_extent,
      .activation_allowed = true,
  };
  return NPU_RUNTIME_SUCCESS;
}

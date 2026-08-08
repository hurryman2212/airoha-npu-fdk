/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_reset.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static uint32_t read_shared_word(const volatile uint32_t *word) {
  uint32_t value;

  an7581_dma_memory_barrier();
  value = *word;
  an7581_dma_memory_barrier();
  return value;
}

static enum npu_runtime_result
wait_for_result_drain(struct npu_wifi_rro_reset *reset) {
  uint32_t poll_count;
  enum npu_runtime_result status;

  for (poll_count = 0U; poll_count < reset->poll_limit; ++poll_count) {
    if (read_shared_word(reset->result_target) ==
        read_shared_word(reset->result_observed)) {
      reset->last_drain_poll_count = poll_count + 1U;
      return NPU_RUNTIME_SUCCESS;
    }
    status = reset->delay(reset->delay_context, NPU_WIFI_RRO_RESET_DRAIN_DELAY);
    if (status != NPU_RUNTIME_SUCCESS) {
      reset->last_drain_poll_count = poll_count + 1U;
      return status;
    }
  }
  reset->last_drain_poll_count = reset->poll_limit;
  return NPU_RUNTIME_TIMEOUT;
}

static enum npu_runtime_result
wait_for_allocator_idle(struct npu_wifi_rro_reset *reset) {
  uint32_t consecutive_idle_count = 0U;
  uint32_t poll_count;
  enum npu_runtime_result status;

  status = reset->delay(reset->delay_context, NPU_WIFI_RRO_RESET_IDLE_DELAY);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  for (poll_count = 0U; poll_count < reset->poll_limit; ++poll_count) {
    if ((read_shared_word(reset->allocator_activity) & UINT32_C(0xffff)) == 0U)
      ++consecutive_idle_count;
    else
      consecutive_idle_count = 0U;

    status = reset->delay(reset->delay_context, NPU_WIFI_RRO_RESET_IDLE_DELAY);
    if (status != NPU_RUNTIME_SUCCESS) {
      reset->last_activity_poll_count = poll_count + 1U;
      return status;
    }
    if (consecutive_idle_count == 2U) {
      reset->last_activity_poll_count = poll_count + 1U;
      return NPU_RUNTIME_SUCCESS;
    }
  }
  reset->last_activity_poll_count = reset->poll_limit;
  return NPU_RUNTIME_TIMEOUT;
}

static bool
reset_configuration_is_valid(const struct npu_wifi_rro_reset *reset) {
  return reset != NULL && reset->result_target != NULL &&
         reset->result_observed != NULL && reset->allocator_activity != NULL &&
         reset->delay != NULL && reset->reset_packet_ids != NULL &&
         reset->reset_buffer_ids != NULL && reset->poll_limit >= 2U;
}

enum npu_runtime_result
npu_wifi_rro_reset_initialize(struct npu_wifi_rro_reset *reset,
                              const struct npu_wifi_rro_reset_config *config) {
  if (reset == NULL || config == NULL || config->result_target == NULL ||
      config->result_observed == NULL || config->allocator_activity == NULL ||
      config->delay == NULL || config->reset_packet_ids == NULL ||
      config->reset_buffer_ids == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)config->result_target & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)config->result_observed & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)config->allocator_activity & (sizeof(uint32_t) - 1U)) != 0U ||
      config->poll_limit < 2U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(reset, 0U, sizeof(*reset));
  reset->result_target = config->result_target;
  reset->result_observed = config->result_observed;
  reset->allocator_activity = config->allocator_activity;
  reset->delay = config->delay;
  reset->reset_packet_ids = config->reset_packet_ids;
  reset->reset_buffer_ids = config->reset_buffer_ids;
  reset->delay_context = config->delay_context;
  reset->packet_id_context = config->packet_id_context;
  reset->buffer_id_context = config->buffer_id_context;
  reset->poll_limit = config->poll_limit;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_reset_apply(void *context) {
  struct npu_wifi_rro_reset *reset = context;
  enum npu_runtime_result status;

  if (reset == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!reset_configuration_is_valid(reset))
    return NPU_RUNTIME_OUT_OF_RANGE;

  reset->last_drain_poll_count = 0U;
  reset->last_activity_poll_count = 0U;
  status = wait_for_result_drain(reset);
  if (status == NPU_RUNTIME_SUCCESS)
    status = wait_for_allocator_idle(reset);
  if (status == NPU_RUNTIME_SUCCESS)
    status = reset->reset_packet_ids(reset->packet_id_context);
  if (status == NPU_RUNTIME_SUCCESS)
    status = reset->reset_buffer_ids(reset->buffer_id_context);

  if (status == NPU_RUNTIME_SUCCESS)
    ++reset->completed_reset_count;
  else if (status == NPU_RUNTIME_TIMEOUT)
    ++reset->timeout_count;
  else
    ++reset->failure_count;
  return status;
}

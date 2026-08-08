/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_done_descriptors.h"

#include "an7581/runtime/memory.h"

static bool
configuration_is_valid(const struct npu_wifi_tx_done_descriptor_state *state,
                       const struct npu_wifi_tx_done_descriptor_config *config,
                       const struct npu_wifi_rx_ring_profile *ring_profile,
                       uint32_t descriptor_count) {
  uint32_t required_capacity = descriptor_count;
  size_t required_descriptor_size;

  if (ring_profile == NULL || ring_profile->kind != NPU_WIFI_RX_RING_TX_DONE ||
      ring_profile->packet_data_offset != UINT32_C(0x80) ||
      descriptor_count == 0U ||
      descriptor_count > ring_profile->maximum_descriptor_count ||
      config->descriptor_memory == NULL || config->packet_ids == NULL ||
      config->operations.packet_ids.allocate == NULL ||
      config->operations.packet_ids.release == NULL ||
      config->operations.force_reset_token_ids == NULL)
    return false;

  if (((uintptr_t)config->descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)config->packet_ids & (sizeof(uint16_t) - 1U)) != 0U)
    return false;

  if (state->ready)
    return false;

  required_descriptor_size =
      (size_t)required_capacity * ring_profile->descriptor_size;
  return config->packet_id_capacity >= required_capacity &&
         config->descriptor_memory_size >= required_descriptor_size;
}

enum npu_runtime_result npu_wifi_tx_done_descriptors_initialize(
    struct npu_wifi_tx_done_descriptor_state *state,
    const struct npu_wifi_tx_done_descriptor_config *config,
    uint32_t descriptor_count) {
  const struct npu_wifi_rx_ring_profile *ring_profile;
  enum npu_runtime_result status;

  if (state == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  ring_profile = npu_wifi_rx_ring_find_profile(10U);
  if (!configuration_is_valid(state, config, ring_profile, descriptor_count))
    return state->ready ? NPU_RUNTIME_REJECTED : NPU_RUNTIME_OUT_OF_RANGE;

  if (state->descriptor_count != 0U) {
    if (state->producer != 0U ||
        (uint32_t)state->descriptor_count != descriptor_count)
      return NPU_RUNTIME_REJECTED;

    status =
        config->operations.force_reset_token_ids(config->force_reset_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    state->ready = true;
    return NPU_RUNTIME_SUCCESS;
  }

  *state = (struct npu_wifi_tx_done_descriptor_state){0};
  status = npu_wifi_rx_ring_initialize(
      ring_profile, config->packet_buffer_base, config->descriptor_memory,
      config->descriptor_memory_size, descriptor_count, config->packet_ids,
      config->packet_id_capacity, &config->operations.packet_ids,
      config->packet_id_context);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  state->producer = 0U;
  state->descriptor_count = (uint16_t)descriptor_count;
  status =
      config->operations.force_reset_token_ids(config->force_reset_context);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  state->ready = true;
  return NPU_RUNTIME_SUCCESS;
}

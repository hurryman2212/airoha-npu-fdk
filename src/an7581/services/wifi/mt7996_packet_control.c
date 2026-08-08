/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_packet_control.h"

#include "an7581/runtime/memory.h"

static bool packet_mapping_is_valid(
    const struct npu_wifi_mt7996_packet_control_config *config) {
  uint32_t packet_span;

  if (config->packet_mapping == NULL ||
      ((uintptr_t)config->packet_mapping & (sizeof(uint32_t) - 1U)) != 0U ||
      config->packet_count == 0U || config->packet_count > UINT32_C(0x10000))
    return false;

  packet_span =
      config->packet_count * NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE;
  return config->packet_mapping_size >= packet_span;
}

enum npu_runtime_result npu_wifi_mt7996_packet_control_initialize(
    struct npu_wifi_mt7996_packet_control *control,
    const struct npu_wifi_mt7996_packet_control_config *config) {
  if (control == NULL || config == NULL || config->enqueue == NULL ||
      config->release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (control->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!packet_mapping_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(control, 0U, sizeof(*control));
  control->packet_mapping = config->packet_mapping;
  control->enqueue = config->enqueue;
  control->release = config->release;
  control->packet_context = config->packet_context;
  control->packet_mapping_size = config->packet_mapping_size;
  control->packet_count = config->packet_count;
  control->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_mt7996_packet_control_enqueue(
    struct npu_wifi_mt7996_packet_control *control, int16_t packet_id,
    uint16_t flow_value, uint8_t route) {
  volatile uint32_t *packet_control;
  uint16_t packet_length;

  if (control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!control->initialized)
    return NPU_RUNTIME_REJECTED;
  if (packet_id < 0 || (uint32_t)packet_id >= control->packet_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  packet_control = __builtin_assume_aligned(
      control->packet_mapping +
          (uint32_t)packet_id * NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE,
      sizeof(uint32_t));
  packet_length = (uint16_t)((*packet_control & UINT32_C(0x1ffff)) >> 3U);
  return control->enqueue(
      control->packet_context, packet_id, packet_length, flow_value, route, 1U,
      NPU_WIFI_MT7996_PACKET_CONTROL_QUEUE_FLAGS, packet_length);
}

enum npu_runtime_result npu_wifi_mt7996_packet_control_release(
    struct npu_wifi_mt7996_packet_control *control, uint16_t packet_id) {
  if (control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!control->initialized)
    return NPU_RUNTIME_REJECTED;
  if ((uint32_t)packet_id >= control->packet_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return control->release(control->packet_context, packet_id);
}

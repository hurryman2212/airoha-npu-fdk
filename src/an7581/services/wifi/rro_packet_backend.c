/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_packet_backend.h"

#include "an7581/runtime/memory.h"

enum npu_runtime_result npu_wifi_rro_packet_backend_initialize(
    struct npu_wifi_rro_packet_backend *backend, volatile void *packet_mapping,
    uint32_t buffer_count, npu_wifi_rro_packet_dispatch_backend dispatch,
    void *dispatch_context, npu_wifi_rro_packet_release_backend release,
    void *release_context) {
  if (backend == NULL || dispatch == NULL || release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (packet_mapping == NULL ||
      ((uintptr_t)packet_mapping & (sizeof(uint32_t) - 1U)) != 0U ||
      buffer_count == 0U || buffer_count > UINT32_C(0x10000))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(backend, 0U, sizeof(*backend));
  backend->packet_mapping = packet_mapping;
  backend->dispatch = dispatch;
  backend->release = release;
  backend->dispatch_context = dispatch_context;
  backend->release_context = release_context;
  backend->buffer_count = buffer_count;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result write_control(void *context, uint16_t buffer_id,
                                             uint32_t packet_control) {
  struct npu_wifi_rro_packet_backend *backend = context;
  volatile uint32_t *control;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if ((uint32_t)buffer_id >= backend->buffer_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  control = __builtin_assume_aligned(backend->packet_mapping +
                                         (uint32_t)buffer_id *
                                             NPU_WIFI_RRO_PACKET_BUFFER_STRIDE,
                                     sizeof(uint32_t));
  *control = packet_control;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result dispatch_packet(void *context, int16_t buffer_id,
                                               uint16_t total_length,
                                               uint8_t flags,
                                               uint16_t fragment_length) {
  struct npu_wifi_rro_packet_backend *backend = context;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (buffer_id < 0 || (uint32_t)buffer_id >= backend->buffer_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return backend->dispatch(backend->dispatch_context, buffer_id, total_length,
                           flags, fragment_length);
}

static enum npu_runtime_result release_packet(void *context,
                                              uint16_t buffer_id) {
  struct npu_wifi_rro_packet_backend *backend = context;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if ((uint32_t)buffer_id >= backend->buffer_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return backend->release(backend->release_context, buffer_id);
}

const struct npu_wifi_rro_fragment_operations
    npu_wifi_rro_packet_backend_operations = {
        .write_control = write_control,
        .dispatch = dispatch_packet,
        .release = release_packet,
};

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/packet_id_backend.h"

#include "an7581/runtime/memory.h"

enum npu_runtime_result npu_wifi_packet_id_backend_initialize(
    struct npu_wifi_packet_id_backend *backend,
    struct npu_wifi_packet_id_pool *pool) {
  if (backend == NULL || pool == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!pool->initialized || pool->token_entries == NULL ||
      pool->recycle_entries == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(backend, 0U, sizeof(*backend));
  backend->pool = pool;
  backend->last_allocation_status = NPU_RUNTIME_SUCCESS;
  backend->last_release_status = NPU_RUNTIME_SUCCESS;
  return NPU_RUNTIME_SUCCESS;
}

static bool allocate_packet_id(void *context, uint16_t *packet_id) {
  struct npu_wifi_packet_id_backend *backend = context;
  enum npu_runtime_result status;

  if (backend == NULL)
    return false;
  if (backend->pool == NULL) {
    backend->last_allocation_status = NPU_RUNTIME_OUT_OF_RANGE;
    ++backend->allocation_failure_count;
    return false;
  }

  status = npu_wifi_packet_id_pool_allocate(backend->pool, packet_id);
  backend->last_allocation_status = status;
  if (status != NPU_RUNTIME_SUCCESS)
    ++backend->allocation_failure_count;
  return status == NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_packet_id_backend_release(void *context,
                                                           uint16_t packet_id) {
  struct npu_wifi_packet_id_backend *backend = context;
  enum npu_runtime_result status;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (backend->pool == NULL)
    status = NPU_RUNTIME_OUT_OF_RANGE;
  else
    status = npu_wifi_packet_id_pool_release(backend->pool, packet_id);
  backend->last_release_status = status;
  if (status != NPU_RUNTIME_SUCCESS)
    ++backend->release_failure_count;
  return status;
}

static void release_packet_id(void *context, uint16_t packet_id) {
  (void)npu_wifi_packet_id_backend_release(context, packet_id);
}

const struct npu_wifi_rx_buffer_operations
    npu_wifi_packet_id_backend_operations = {
        .allocate = allocate_packet_id,
        .release = release_packet_id,
};

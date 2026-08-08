/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_PACKET_ID_BACKEND_H
#define NPU_WIFI_PACKET_ID_BACKEND_H

#include "an7581/services/wifi/packet_id_pool.h"
#include "an7581/services/wifi/rx_ring.h"

struct npu_wifi_packet_id_backend {
  struct npu_wifi_packet_id_pool *pool;
  enum npu_runtime_result last_allocation_status;
  enum npu_runtime_result last_release_status;
  uint32_t allocation_failure_count;
  uint32_t release_failure_count;
};

enum npu_runtime_result npu_wifi_packet_id_backend_initialize(
    struct npu_wifi_packet_id_backend *backend,
    struct npu_wifi_packet_id_pool *pool);
enum npu_runtime_result npu_wifi_packet_id_backend_release(void *context,
                                                           uint16_t packet_id);

extern const struct npu_wifi_rx_buffer_operations
    npu_wifi_packet_id_backend_operations;

#endif

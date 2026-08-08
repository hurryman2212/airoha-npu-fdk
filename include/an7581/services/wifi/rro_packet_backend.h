/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_PACKET_BACKEND_H
#define NPU_WIFI_RRO_PACKET_BACKEND_H

#include "an7581/services/wifi/rro_packet.h"

#define NPU_WIFI_RRO_PACKET_BUFFER_STRIDE UINT32_C(0x800)

typedef enum npu_runtime_result (*npu_wifi_rro_packet_dispatch_backend)(
    void *context, int16_t buffer_id, uint16_t total_length, uint8_t flags,
    uint16_t fragment_length);
typedef enum npu_runtime_result (*npu_wifi_rro_packet_release_backend)(
    void *context, uint16_t buffer_id);

struct npu_wifi_rro_packet_backend {
  volatile uint8_t *packet_mapping;
  npu_wifi_rro_packet_dispatch_backend dispatch;
  npu_wifi_rro_packet_release_backend release;
  void *dispatch_context;
  void *release_context;
  uint32_t buffer_count;
};

enum npu_runtime_result npu_wifi_rro_packet_backend_initialize(
    struct npu_wifi_rro_packet_backend *backend, volatile void *packet_mapping,
    uint32_t buffer_count, npu_wifi_rro_packet_dispatch_backend dispatch,
    void *dispatch_context, npu_wifi_rro_packet_release_backend release,
    void *release_context);

extern const struct npu_wifi_rro_fragment_operations
    npu_wifi_rro_packet_backend_operations;

#endif

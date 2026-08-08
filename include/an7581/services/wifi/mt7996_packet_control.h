/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_PACKET_CONTROL_H
#define NPU_WIFI_MT7996_PACKET_CONTROL_H

#include "an7581/services/wifi/rro_cpu_queue.h"

#define NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE UINT32_C(0x800)
#define NPU_WIFI_MT7996_PACKET_CONTROL_QUEUE_FLAGS UINT8_C(2)

typedef enum npu_runtime_result (
    *npu_wifi_mt7996_packet_queue_enqueue_callback)(
    void *context, int16_t packet_id, uint16_t total_length,
    uint16_t flow_value, uint16_t route, uint8_t band, uint8_t flags,
    uint16_t fragment_length);

struct npu_wifi_mt7996_packet_control_config {
  volatile uint8_t *packet_mapping;
  npu_wifi_mt7996_packet_queue_enqueue_callback enqueue;
  npu_wifi_rro_cpu_queue_release release;
  void *packet_context;
  size_t packet_mapping_size;
  uint32_t packet_count;
};

struct npu_wifi_mt7996_packet_control {
  volatile uint8_t *packet_mapping;
  npu_wifi_mt7996_packet_queue_enqueue_callback enqueue;
  npu_wifi_rro_cpu_queue_release release;
  void *packet_context;
  size_t packet_mapping_size;
  uint32_t packet_count;
  bool initialized;
};

enum npu_runtime_result npu_wifi_mt7996_packet_control_initialize(
    struct npu_wifi_mt7996_packet_control *control,
    const struct npu_wifi_mt7996_packet_control_config *config);
enum npu_runtime_result npu_wifi_mt7996_packet_control_enqueue(
    struct npu_wifi_mt7996_packet_control *control, int16_t packet_id,
    uint16_t flow_value, uint8_t route);
enum npu_runtime_result npu_wifi_mt7996_packet_control_release(
    struct npu_wifi_mt7996_packet_control *control, uint16_t packet_id);

#endif

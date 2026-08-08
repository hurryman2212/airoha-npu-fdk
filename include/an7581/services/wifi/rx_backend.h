/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RX_BACKEND_H
#define NPU_WIFI_RX_BACKEND_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/region.h"

struct npu_wifi_rx_arena {
  void *descriptor_memory;
  uint16_t *buffer_ids;
  const struct npu_wifi_rx_buffer_operations *buffer_operations;
  void *buffer_context;
  struct npu_wifi_msdu_page_descriptor_state msdu_page_state;
  size_t descriptor_memory_size;
  uint32_t physical_base;
  uint32_t packet_buffer_base;
  uint32_t buffer_id_capacity;
  uint32_t initialized_descriptor_count;
  uint8_t set_interface;
};

struct npu_wifi_rx_static_backend {
  struct npu_wifi_rx_arena *arenas;
  const struct npu_wifi_rx_buffer_operations *buffer_operations;
  void *buffer_context;
  size_t arena_count;
  uint32_t dynamic_base;
};

extern const struct npu_wifi_backend_operations
    npu_wifi_rx_static_backend_operations;

#endif

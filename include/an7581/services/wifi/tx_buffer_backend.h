/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_BUFFER_BACKEND_H
#define NPU_WIFI_TX_BUFFER_BACKEND_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/tx_buffer_space.h"

struct npu_wifi_tx_buffer_space_arena {
  void *record_memory;
  size_t record_memory_size;
  uint32_t physical_base;
  uint8_t set_interface;
  bool initialized;
};

struct npu_wifi_tx_buffer_space_backend {
  struct npu_wifi_tx_buffer_space_arena *arenas;
  bool (*activate_packet_space)(void *context, uint32_t activation_index,
                                uint32_t local_address);
  void *activation_context;
  size_t arena_count;
  uint32_t dynamic_base;
  uint32_t local_address[NPU_WIFI_INTERFACE_COUNT];
  uint32_t valid_interfaces;
};

extern const struct npu_wifi_backend_operations
    npu_wifi_tx_buffer_space_backend_operations;

#endif

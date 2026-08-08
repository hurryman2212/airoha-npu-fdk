/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_PACKET_BACKEND_H
#define NPU_WIFI_TX_PACKET_BACKEND_H

#include "an7581/services/wifi/tx_packet_space.h"

struct npu_wifi_tx_packet_arena {
  void *descriptor_memory;
  size_t descriptor_memory_size;
  uint32_t physical_base;
  uint8_t activation_index;
  bool initialized;
};

struct npu_wifi_tx_packet_static_backend {
  struct npu_wifi_tx_packet_arena *arenas;
  size_t arena_count;
};

bool npu_wifi_tx_packet_static_backend_activate(void *context,
                                                uint32_t activation_index,
                                                uint32_t local_address);

#endif

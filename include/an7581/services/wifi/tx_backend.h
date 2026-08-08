/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_BACKEND_H
#define NPU_WIFI_TX_BACKEND_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/tx_ring.h"

struct npu_wifi_tx_arena {
  void *descriptor_memory;
  size_t descriptor_memory_size;
  uint32_t physical_base;
  uint8_t get_interface;
  bool initialized;
};

struct npu_wifi_tx_static_backend {
  struct npu_wifi_tx_arena *arenas;
  size_t arena_count;
  uint32_t dynamic_base;
};

bool npu_wifi_tx_static_backend_initialize_ring(
    struct npu_wifi_configuration *configuration,
    struct npu_wifi_tx_static_backend *backend, uint32_t get_interface);
bool npu_wifi_tx_static_backend_prepare_descriptor_base(
    struct npu_wifi_tx_static_backend *backend, uint32_t get_interface);
bool npu_wifi_tx_static_backend_release_ring(
    struct npu_wifi_configuration *configuration,
    struct npu_wifi_tx_static_backend *backend, uint32_t get_interface);

#endif

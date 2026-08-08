/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_EAGLE_TX_BACKEND_H
#define NPU_WIFI_EAGLE_TX_BACKEND_H

#include "an7581/services/wifi/eagle_tx.h"
#include "an7581/services/wifi/mailbox.h"

struct npu_wifi_eagle_tx_backend {
  npu_wifi_eagle_tx_write32 write32;
  void *write_context;
  npu_wifi_eagle_tx_read32 read32;
  void *read_context;
  uint32_t dynamic_base;
  uint32_t tx_ring_pcie_address[NPU_WIFI_INTERFACE_COUNT];
  uint32_t valid_tx_ring_interfaces;
  uint32_t programmed_interfaces;
  uint16_t tx_initialize_state;
  uint16_t descriptor_initialize_state;
  uint8_t pcie_port_type;
  bool pcie_port_type_valid;
};

extern const struct npu_wifi_backend_operations
    npu_wifi_eagle_tx_backend_operations;

bool npu_wifi_eagle_tx_backend_initialize(
    struct npu_wifi_eagle_tx_backend *backend, uint32_t dynamic_base,
    npu_wifi_eagle_tx_write32 write32, void *write_context);
bool npu_wifi_eagle_tx_backend_initialize_verified(
    struct npu_wifi_eagle_tx_backend *backend, uint32_t dynamic_base,
    npu_wifi_eagle_tx_write32 write32, void *write_context,
    npu_wifi_eagle_tx_read32 read32, void *read_context);

#endif

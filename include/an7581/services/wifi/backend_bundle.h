/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_BACKEND_BUNDLE_H
#define NPU_WIFI_BACKEND_BUNDLE_H

#include "an7581/services/wifi/mailbox.h"

struct npu_wifi_backend_binding {
  const struct npu_wifi_backend_operations *operations;
  void *context;
};

struct npu_wifi_backend_bundle {
  struct npu_wifi_backend_binding pcie_address;
  struct npu_wifi_backend_binding pcie_port_type;
  struct npu_wifi_backend_binding force_to_cpu;
  struct npu_wifi_backend_binding rx_ring;
  struct npu_wifi_backend_binding rx_descriptor_base;
  struct npu_wifi_backend_binding tx_ring_pcie_address;
  struct npu_wifi_backend_binding tx_descriptor_base;
  struct npu_wifi_backend_binding tx_buffer_space_base;
  struct npu_wifi_backend_binding tx_done_ring_base;
  struct npu_wifi_backend_binding delete_station;
  struct npu_wifi_backend_binding dram_ba_node_address;
  struct npu_wifi_backend_binding inode_txrx_registers;
};

bool npu_wifi_backend_bundle_initialize(
    struct npu_wifi_backend_bundle *bundle,
    const struct npu_wifi_backend_binding *components, size_t component_count);
bool npu_wifi_backend_bundle_bind(
    struct npu_wifi_backend_bundle *bundle,
    const struct npu_wifi_backend_binding *components, size_t component_count);

extern const struct npu_wifi_backend_operations
    npu_wifi_backend_bundle_operations;

#endif

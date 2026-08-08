/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RX_PCIE_BACKEND_H
#define NPU_WIFI_RX_PCIE_BACKEND_H

#include "an7581/services/wifi/rx_pcie.h"

struct npu_wifi_rx_pcie_backend {
  struct npu_wifi_rx_pcie_state state;
  struct npu_wifi_configuration *configuration;
  npu_wifi_rx_pcie_write32 write32;
  void *write_context;
};

extern const struct npu_wifi_backend_operations
    npu_wifi_rx_pcie_backend_operations;

bool npu_wifi_rx_pcie_backend_initialize(
    struct npu_wifi_rx_pcie_backend *backend,
    struct npu_wifi_configuration *configuration,
    npu_wifi_rx_pcie_write32 write32, void *write_context);

#endif

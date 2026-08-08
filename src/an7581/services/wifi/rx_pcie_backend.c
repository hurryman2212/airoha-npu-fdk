/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rx_pcie_backend.h"

#include "an7581/services/wifi/mt7996_mailbox_interface.h"

static bool set_pcie_address(void *context, uint32_t interface,
                             uint32_t address) {
  struct npu_wifi_rx_pcie_backend *backend = context;
  const struct npu_wifi_interface_configuration *tx_done;
  bool descriptor_count_valid;

  if (backend == NULL || backend->configuration == NULL)
    return false;

  tx_done = &backend->configuration
                 ->interface[NPU_WIFI_MT7996_TX_DONE_REGISTER_INTERFACE];
  descriptor_count_valid =
      (tx_done->valid_fields & NPU_WIFI_VALID_DESCRIPTOR_COUNT) != 0U;
  return npu_wifi_rx_pcie_apply(
      &backend->state, interface, address, tx_done->descriptor_count,
      descriptor_count_valid, backend->write32, backend->write_context);
}

const struct npu_wifi_backend_operations npu_wifi_rx_pcie_backend_operations = {
    .set_pcie_address = set_pcie_address,
};

bool npu_wifi_rx_pcie_backend_initialize(
    struct npu_wifi_rx_pcie_backend *backend,
    struct npu_wifi_configuration *configuration,
    npu_wifi_rx_pcie_write32 write32, void *write_context) {
  if (backend == NULL || configuration == NULL || write32 == NULL ||
      !npu_wifi_rx_pcie_initialize(&backend->state))
    return false;

  backend->configuration = configuration;
  backend->write32 = write32;
  backend->write_context = write_context;
  return true;
}

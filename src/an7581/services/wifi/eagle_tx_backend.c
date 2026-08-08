/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/eagle_tx_backend.h"

#include "an7581/runtime/memory.h"

#define NPU_WIFI_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)

static bool program_band(struct npu_wifi_eagle_tx_backend *backend,
                         uint32_t pcie_port_type, uint32_t band) {
  const struct npu_wifi_eagle_tx_initialize_context context = {
      .write32 = backend->write32,
      .write_context = backend->write_context,
      .read32 = backend->read32,
      .read_context = backend->read_context,
      .tx_state = &backend->tx_initialize_state,
      .descriptor_state = &backend->descriptor_initialize_state,
  };

  return npu_wifi_eagle_tx_initialize(backend->dynamic_base, pcie_port_type,
                                      band, &context);
}

static bool set_pcie_port_type(void *context, uint32_t pcie_port_type) {
  struct npu_wifi_eagle_tx_backend *backend = context;

  if (backend == NULL || pcie_port_type >= NPU_WIFI_PCIE_PORT_TYPE_COUNT)
    return false;

  backend->pcie_port_type = (uint8_t)pcie_port_type;
  backend->pcie_port_type_valid = true;
  return true;
}

static bool set_tx_ring_pcie_address(void *context, uint32_t interface,
                                     uint32_t address) {
  struct npu_wifi_eagle_tx_backend *backend = context;
  bool initializes_window;
  uint32_t passive_interface;

  if (backend == NULL || interface >= NPU_WIFI_INTERFACE_COUNT ||
      address > NPU_WIFI_MAX_HOST_ADDRESS)
    return false;

  initializes_window = interface == 0U || interface == 2U;
  passive_interface = 3U;

  if (!initializes_window && interface != passive_interface)
    return false;
  if (initializes_window && !backend->pcie_port_type_valid)
    return false;
  if (initializes_window &&
      !program_band(backend, backend->pcie_port_type, interface))
    return false;

  backend->tx_ring_pcie_address[interface] = address;
  backend->valid_tx_ring_interfaces |= UINT32_C(1) << interface;
  if (initializes_window)
    backend->programmed_interfaces |= UINT32_C(1) << interface;
  return true;
}

const struct npu_wifi_backend_operations npu_wifi_eagle_tx_backend_operations =
    {
        .set_pcie_port_type = set_pcie_port_type,
        .set_tx_ring_pcie_address = set_tx_ring_pcie_address,
};

static bool backend_initialize(struct npu_wifi_eagle_tx_backend *backend,
                               uint32_t dynamic_base,
                               npu_wifi_eagle_tx_write32 write32,
                               void *write_context,
                               npu_wifi_eagle_tx_read32 read32,
                               void *read_context) {
  struct npu_wifi_eagle_tx_window window;

  if (backend == NULL || write32 == NULL ||
      !npu_wifi_eagle_tx_window_plan(dynamic_base, 0U, 0U, &window))
    return false;

  (void)npu_memset(backend, 0U, sizeof(*backend));
  backend->write32 = write32;
  backend->write_context = write_context;
  backend->read32 = read32;
  backend->read_context = read_context;
  backend->dynamic_base = dynamic_base;
  return true;
}

bool npu_wifi_eagle_tx_backend_initialize(
    struct npu_wifi_eagle_tx_backend *backend, uint32_t dynamic_base,
    npu_wifi_eagle_tx_write32 write32, void *write_context) {
  return backend_initialize(backend, dynamic_base, write32, write_context, NULL,
                            NULL);
}

bool npu_wifi_eagle_tx_backend_initialize_verified(
    struct npu_wifi_eagle_tx_backend *backend, uint32_t dynamic_base,
    npu_wifi_eagle_tx_write32 write32, void *write_context,
    npu_wifi_eagle_tx_read32 read32, void *read_context) {
  if (read32 == NULL)
    return false;

  return backend_initialize(backend, dynamic_base, write32, write_context,
                            read32, read_context);
}

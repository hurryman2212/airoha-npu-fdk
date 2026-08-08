/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/backend_bundle.h"

#include "an7581/runtime/memory.h"

static bool bind_operation(struct npu_wifi_backend_binding *destination,
                           const struct npu_wifi_backend_binding *component,
                           bool operation_present, bool *component_used) {
  if (!operation_present)
    return true;
  if (destination->operations != NULL)
    return false;

  *destination = *component;
  *component_used = true;
  return true;
}

static bool bind_components(struct npu_wifi_backend_bundle *bundle,
                            const struct npu_wifi_backend_binding *components,
                            size_t component_count) {
  size_t index;

  if (bundle == NULL || components == NULL || component_count == 0U)
    return false;

  for (index = 0U; index < component_count; ++index) {
    const struct npu_wifi_backend_binding *component = &components[index];
    const struct npu_wifi_backend_operations *operations =
        component->operations;
    bool component_used = false;

    if (operations == NULL ||
        !bind_operation(&bundle->pcie_address, component,
                        operations->set_pcie_address != NULL,
                        &component_used) ||
        !bind_operation(&bundle->pcie_port_type, component,
                        operations->set_pcie_port_type != NULL,
                        &component_used) ||
        !bind_operation(&bundle->force_to_cpu, component,
                        operations->set_force_to_cpu != NULL,
                        &component_used) ||
        !bind_operation(&bundle->rx_ring, component,
                        operations->initialize_rx_ring != NULL,
                        &component_used) ||
        !bind_operation(&bundle->rx_descriptor_base, component,
                        operations->prepare_rx_descriptor_base != NULL,
                        &component_used) ||
        !bind_operation(&bundle->tx_ring_pcie_address, component,
                        operations->set_tx_ring_pcie_address != NULL,
                        &component_used) ||
        !bind_operation(&bundle->tx_descriptor_base, component,
                        operations->set_tx_descriptor_base != NULL,
                        &component_used) ||
        !bind_operation(&bundle->tx_buffer_space_base, component,
                        operations->set_tx_buffer_space_base != NULL,
                        &component_used) ||
        !bind_operation(&bundle->tx_done_ring_base, component,
                        operations->set_tx_done_ring_base != NULL,
                        &component_used) ||
        !bind_operation(&bundle->delete_station, component,
                        operations->set_delete_station != NULL,
                        &component_used) ||
        !bind_operation(&bundle->dram_ba_node_address, component,
                        operations->set_dram_ba_node_address != NULL,
                        &component_used) ||
        !bind_operation(&bundle->inode_txrx_registers, component,
                        operations->set_inode_txrx_registers != NULL,
                        &component_used) ||
        !component_used)
      return false;
  }

  return true;
}

bool npu_wifi_backend_bundle_initialize(
    struct npu_wifi_backend_bundle *bundle,
    const struct npu_wifi_backend_binding *components, size_t component_count) {
  struct npu_wifi_backend_bundle candidate;

  if (bundle == NULL)
    return false;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  if (!bind_components(&candidate, components, component_count))
    return false;

  *bundle = candidate;
  return true;
}

bool npu_wifi_backend_bundle_bind(
    struct npu_wifi_backend_bundle *bundle,
    const struct npu_wifi_backend_binding *components, size_t component_count) {
  struct npu_wifi_backend_bundle candidate;

  if (bundle == NULL)
    return false;

  candidate = *bundle;
  if (!bind_components(&candidate, components, component_count))
    return false;

  *bundle = candidate;
  return true;
}

static bool set_pcie_address(void *context, uint32_t interface,
                             uint32_t address) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->pcie_address;
  return binding->operations == NULL ||
         binding->operations->set_pcie_address(binding->context, interface,
                                               address);
}

static bool set_pcie_port_type(void *context, uint32_t pcie_port_type) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->pcie_port_type;
  return binding->operations == NULL || binding->operations->set_pcie_port_type(
                                            binding->context, pcie_port_type);
}

static bool set_force_to_cpu(void *context, bool force_to_cpu) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->force_to_cpu;
  return binding->operations == NULL ||
         binding->operations->set_force_to_cpu(binding->context, force_to_cpu);
}

static bool initialize_rx_ring(void *context, uint32_t interface,
                               uint32_t descriptor_count,
                               uint32_t *rx_descriptor_base) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL || rx_descriptor_base == NULL)
    return false;
  binding = &bundle->rx_ring;
  return binding->operations != NULL &&
         binding->operations->initialize_rx_ring(
             binding->context, interface, descriptor_count, rx_descriptor_base);
}

static bool prepare_rx_descriptor_base(void *context, uint32_t interface) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->rx_descriptor_base;
  return binding->operations == NULL ||
         binding->operations->prepare_rx_descriptor_base(binding->context,
                                                         interface);
}

static bool set_tx_ring_pcie_address(void *context, uint32_t interface,
                                     uint32_t address) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->tx_ring_pcie_address;
  return binding->operations == NULL ||
         binding->operations->set_tx_ring_pcie_address(binding->context,
                                                       interface, address);
}

static bool set_tx_descriptor_base(void *context, uint32_t interface,
                                   uint32_t address) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->tx_descriptor_base;
  return binding->operations == NULL ||
         binding->operations->set_tx_descriptor_base(binding->context,
                                                     interface, address);
}

static bool set_tx_buffer_space_base(void *context, uint32_t interface,
                                     uint32_t address) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->tx_buffer_space_base;
  return binding->operations == NULL ||
         binding->operations->set_tx_buffer_space_base(binding->context,
                                                       interface, address);
}

static bool set_tx_done_ring_base(void *context, uint32_t interface,
                                  uint32_t address) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->tx_done_ring_base;
  return binding->operations == NULL ||
         binding->operations->set_tx_done_ring_base(binding->context, interface,
                                                    address);
}

static bool set_delete_station(void *context, uint32_t action, uint32_t value) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->delete_station;
  return binding->operations == NULL || binding->operations->set_delete_station(
                                            binding->context, action, value);
}

static bool set_dram_ba_node_address(void *context, uint32_t address) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL)
    return false;
  binding = &bundle->dram_ba_node_address;
  return binding->operations == NULL ||
         binding->operations->set_dram_ba_node_address(binding->context,
                                                       address);
}

static bool
set_inode_txrx_registers(void *context, uint32_t interface,
                         const struct npu_wifi_inode_registers *registers) {
  struct npu_wifi_backend_bundle *bundle = context;
  const struct npu_wifi_backend_binding *binding;

  if (bundle == NULL || registers == NULL)
    return false;
  binding = &bundle->inode_txrx_registers;
  return binding->operations == NULL ||
         binding->operations->set_inode_txrx_registers(binding->context,
                                                       interface, registers);
}

const struct npu_wifi_backend_operations npu_wifi_backend_bundle_operations = {
    .set_pcie_address = set_pcie_address,
    .set_pcie_port_type = set_pcie_port_type,
    .set_force_to_cpu = set_force_to_cpu,
    .initialize_rx_ring = initialize_rx_ring,
    .prepare_rx_descriptor_base = prepare_rx_descriptor_base,
    .set_tx_ring_pcie_address = set_tx_ring_pcie_address,
    .set_tx_descriptor_base = set_tx_descriptor_base,
    .set_tx_buffer_space_base = set_tx_buffer_space_base,
    .set_tx_done_ring_base = set_tx_done_ring_base,
    .set_delete_station = set_delete_station,
    .set_dram_ba_node_address = set_dram_ba_node_address,
    .set_inode_txrx_registers = set_inode_txrx_registers,
};

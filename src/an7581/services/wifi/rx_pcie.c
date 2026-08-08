/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rx_pcie.h"

#include "an7581/runtime/memory.h"

#define NPU_WIFI_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)

static bool register_base_is_valid(uint32_t register_base) {
  return register_base != 0U &&
         register_base <=
             NPU_WIFI_MAX_HOST_ADDRESS - NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET &&
         (register_base & (sizeof(uint32_t) - 1U)) == 0U;
}

static bool add_write(struct npu_wifi_rx_pcie_write_plan *plan,
                      uint32_t interface, uint32_t register_base,
                      uint32_t value) {
  struct npu_wifi_rx_pcie_write *write;

  if (plan == NULL || plan->write_count >= NPU_WIFI_RX_PCIE_WRITE_LIMIT ||
      !register_base_is_valid(register_base))
    return false;

  write = &plan->writes[plan->write_count];
  write->address = register_base + NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET;
  write->value = value;
  write->interface = (uint8_t)interface;
  ++plan->write_count;
  return true;
}

static bool cpu_index_is_valid(uint32_t interface, uint32_t value) {
  switch (interface) {
  case 0U:
  case 8U:
    return value == UINT32_C(0x000005ff);
  case 2U:
  case 7U:
    return value == UINT32_C(0x000003ff);
  case 5U:
    return value == UINT32_C(0x000000ff);
  case 6U:
    return value == UINT32_C(0x000001ff);
  case 10U:
    return value < NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT;
  default:
    return false;
  }
}

static bool mt7996_prepare_trigger(const struct npu_wifi_rx_pcie_state *state,
                                   uint32_t tx_done_descriptor_count,
                                   bool tx_done_descriptor_count_valid,
                                   struct npu_wifi_rx_pcie_write_plan *plan) {
  static const uint8_t interfaces[] = {5U, 6U, 7U, 8U, 10U};
  static const uint32_t fixed_cpu_indices[] = {
      UINT32_C(0x000000ff),
      UINT32_C(0x000001ff),
      UINT32_C(0x000003ff),
      UINT32_C(0x000005ff),
  };
  size_t index;

  if (!tx_done_descriptor_count_valid || tx_done_descriptor_count == 0U ||
      tx_done_descriptor_count > NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT)
    return false;

  for (index = 0U; index < sizeof(interfaces) / sizeof(interfaces[0]);
       ++index) {
    uint32_t interface = interfaces[index];
    uint32_t value =
        index < sizeof(fixed_cpu_indices) / sizeof(fixed_cpu_indices[0])
            ? fixed_cpu_indices[index]
            : tx_done_descriptor_count - 1U;

    if ((state->valid_interfaces & (UINT32_C(1) << interface)) == 0U ||
        !add_write(plan, interface, state->register_base[interface], value))
      return false;
  }
  return true;
}

bool npu_wifi_rx_pcie_initialize(struct npu_wifi_rx_pcie_state *state) {
  if (state == NULL)
    return false;

  (void)npu_memset(state, 0U, sizeof(*state));
  return true;
}

bool npu_wifi_rx_pcie_prepare(const struct npu_wifi_rx_pcie_state *state,
                              uint32_t interface, uint32_t register_base,
                              uint32_t tx_done_descriptor_count,
                              bool tx_done_descriptor_count_valid,
                              struct npu_wifi_rx_pcie_write_plan *plan) {
  if (state == NULL || plan == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;

  (void)npu_memset(plan, 0U, sizeof(*plan));
  switch (interface) {
  case 0U:
    return add_write(plan, interface, register_base, UINT32_C(0x000005ff));
  case 2U:
    return add_write(plan, interface, register_base, UINT32_C(0x000003ff));
  case 5U:
  case 6U:
  case 7U:
  case 8U:
  case 10U:
    return register_base_is_valid(register_base);
  case NPU_WIFI_RX_PCIE_TRIGGER_INTERFACE:
    return register_base == 0U &&
           mt7996_prepare_trigger(state, tx_done_descriptor_count,
                                  tx_done_descriptor_count_valid, plan);
  default:
    return false;
  }
}

bool npu_wifi_rx_pcie_program(const struct npu_wifi_rx_pcie_write_plan *plan,
                              npu_wifi_rx_pcie_write32 write32,
                              void *write_context) {
  uint32_t index;

  if (plan == NULL || plan->write_count > NPU_WIFI_RX_PCIE_WRITE_LIMIT ||
      (plan->write_count != 0U && write32 == NULL))
    return false;

  for (index = 0U; index < plan->write_count; ++index) {
    const struct npu_wifi_rx_pcie_write *write = &plan->writes[index];

    if (write->interface >= NPU_WIFI_INTERFACE_COUNT ||
        !register_base_is_valid(write->address -
                                NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET) ||
        !cpu_index_is_valid(write->interface, write->value) ||
        !write32(write_context, write->address, write->value))
      return false;
  }
  return true;
}

bool npu_wifi_rx_pcie_apply(struct npu_wifi_rx_pcie_state *state,
                            uint32_t interface, uint32_t register_base,
                            uint32_t tx_done_descriptor_count,
                            bool tx_done_descriptor_count_valid,
                            npu_wifi_rx_pcie_write32 write32,
                            void *write_context) {
  struct npu_wifi_rx_pcie_write_plan plan;

  if (!npu_wifi_rx_pcie_prepare(state, interface, register_base,
                                tx_done_descriptor_count,
                                tx_done_descriptor_count_valid, &plan) ||
      !npu_wifi_rx_pcie_program(&plan, write32, write_context))
    return false;

  if (interface == NPU_WIFI_RX_PCIE_TRIGGER_INTERFACE)
    return true;

  state->register_base[interface] = register_base;
  state->valid_interfaces |= UINT32_C(1) << interface;
  return true;
}

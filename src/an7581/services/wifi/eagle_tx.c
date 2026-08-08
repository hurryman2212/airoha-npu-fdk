/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/eagle_tx.h"

#include "an7581/services/wifi/region.h"

#define AN7581_PCIE_DOMAIN0_BASE UINT32_C(0x1fc00000)
#define AN7581_PCIE_DOMAIN1_BASE UINT32_C(0x1fc20000)
#define AN7581_PCIE_DOMAIN2_BASE UINT32_C(0x1fc40000)

static bool pcie_domain_base(uint32_t domain, uint32_t *base) {
  if (base == NULL)
    return false;

  switch (domain) {
  case 0U:
    *base = AN7581_PCIE_DOMAIN0_BASE;
    return true;
  case 1U:
    *base = AN7581_PCIE_DOMAIN1_BASE;
    return true;
  case 2U:
    *base = AN7581_PCIE_DOMAIN2_BASE;
    return true;
  default:
    return false;
  }
}

static bool window_boundaries(uint32_t dynamic_base, uint32_t *start,
                              uint32_t *split, uint32_t *end) {
  struct npu_wifi_region region;

  if (start == NULL || split == NULL || end == NULL ||
      !npu_wifi_mt7996_dynamic_region_lookup(
          dynamic_base, NPU_WIFI_MT7996_DYNAMIC_PRIMARY_EAGLE_RX, &region))
    return false;
  *start = region.address;
  if (!npu_wifi_mt7996_dynamic_region_lookup(
          dynamic_base, NPU_WIFI_MT7996_DYNAMIC_SECONDARY_EAGLE_RX, &region) ||
      dynamic_base > UINT32_MAX - NPU_WIFI_MT7996_DYNAMIC_ARENA_SIZE)
    return false;
  *split = region.address;
  *end = dynamic_base + NPU_WIFI_MT7996_DYNAMIC_ARENA_SIZE;
  return true;
}

static bool select_domain(uint32_t pcie_port_type, bool secondary_band,
                          uint32_t *domain) {
  if (domain == NULL || pcie_port_type >= NPU_WIFI_PCIE_PORT_TYPE_COUNT)
    return false;

  switch (pcie_port_type) {
  case 0U:
    *domain = 0U;
    return true;
  case 1U:
    *domain = 1U;
    return true;
  case 2U:
    if (!secondary_band)
      *domain = 0U;
    else
      *domain = 2U;
    return true;
  case 3U:
    *domain = secondary_band ? 0U : 1U;
    return true;
  default:
    return false;
  }
}

bool npu_wifi_eagle_tx_window_plan(uint32_t dynamic_base,
                                   uint32_t pcie_port_type, uint32_t band,
                                   struct npu_wifi_eagle_tx_window *window) {
  uint32_t domain_base;
  uint32_t full_start;
  uint32_t full_end;
  uint32_t split;
  uint32_t domain;
  bool secondary_band;

  if (window == NULL || pcie_port_type >= NPU_WIFI_PCIE_PORT_TYPE_COUNT)
    return false;

  if ((band != 0U && band != 2U) ||
      !window_boundaries(dynamic_base, &full_start, &split, &full_end))
    return false;
  secondary_band = band == 2U;

  if (!select_domain(pcie_port_type, secondary_band, &domain) ||
      !pcie_domain_base(domain, &domain_base))
    return false;

  window->start_register = domain_base + NPU_WIFI_PCIE_TX_WINDOW_START_OFFSET;
  window->end_register = domain_base + NPU_WIFI_PCIE_TX_WINDOW_END_OFFSET;
  if (pcie_port_type < 2U) {
    window->start_address = full_start & NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK;
    window->end_address = full_end & NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK;
  } else if (secondary_band) {
    window->start_address = split & NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK;
    window->end_address = full_end & NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK;
  } else {
    window->start_address = full_start & NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK;
    window->end_address = split & NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK;
  }
  window->band = (uint8_t)band;
  window->pcie_port_type = (uint8_t)pcie_port_type;
  return true;
}

static bool window_is_valid(const struct npu_wifi_eagle_tx_window *window) {
  uint32_t domain;
  uint32_t domain_base;
  bool register_pair_valid = false;

  if (window == NULL || window->start_address >= window->end_address)
    return false;

  for (domain = 0U; domain < 3U; ++domain) {
    if (!pcie_domain_base(domain, &domain_base))
      return false;
    if (window->start_register ==
            domain_base + NPU_WIFI_PCIE_TX_WINDOW_START_OFFSET &&
        window->end_register ==
            domain_base + NPU_WIFI_PCIE_TX_WINDOW_END_OFFSET) {
      register_pair_valid = true;
      break;
    }
  }
  if (!register_pair_valid ||
      (window->start_address & ~NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK) != 0U ||
      (window->end_address & ~NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK) != 0U)
    return false;

  return true;
}

bool npu_wifi_eagle_tx_window_program(
    const struct npu_wifi_eagle_tx_window *window,
    npu_wifi_eagle_tx_write32 write32, void *write_context) {
  if (!window_is_valid(window) || write32 == NULL)
    return false;

  return write32(write_context, window->start_register,
                 window->start_address) &&
         write32(write_context, window->end_register, window->end_address);
}

bool npu_wifi_eagle_tx_window_verify(
    const struct npu_wifi_eagle_tx_window *window,
    npu_wifi_eagle_tx_read32 read32, void *read_context) {
  uint32_t end_address;
  uint32_t start_address;

  if (!window_is_valid(window) || read32 == NULL ||
      !read32(read_context, window->start_register, &start_address) ||
      start_address != window->start_address ||
      !read32(read_context, window->end_register, &end_address))
    return false;

  return end_address == window->end_address;
}

bool npu_wifi_eagle_tx_window_program_verified(
    const struct npu_wifi_eagle_tx_window *window,
    npu_wifi_eagle_tx_write32 write32, void *write_context,
    npu_wifi_eagle_tx_read32 read32, void *read_context) {
  return npu_wifi_eagle_tx_window_program(window, write32, write_context) &&
         npu_wifi_eagle_tx_window_verify(window, read32, read_context);
}

bool npu_wifi_eagle_tx_initialize(
    uint32_t dynamic_base, uint32_t pcie_port_type, uint32_t band,
    const struct npu_wifi_eagle_tx_initialize_context *context) {
  struct npu_wifi_eagle_tx_window window;

  if (context == NULL || context->write32 == NULL ||
      !npu_wifi_eagle_tx_window_plan(dynamic_base, pcie_port_type, band,
                                     &window))
    return false;

  if (context->tx_state == NULL || context->descriptor_state == NULL ||
      ((uintptr_t)context->tx_state & (sizeof(uint16_t) - 1U)) != 0U ||
      ((uintptr_t)context->descriptor_state & (sizeof(uint16_t) - 1U)) != 0U)
    return false;

  *context->tx_state = 0U;
  *context->descriptor_state = 0U;
  if (context->read32 != NULL)
    return npu_wifi_eagle_tx_window_program_verified(
        &window, context->write32, context->write_context, context->read32,
        context->read_context);

  return npu_wifi_eagle_tx_window_program(&window, context->write32,
                                          context->write_context);
}

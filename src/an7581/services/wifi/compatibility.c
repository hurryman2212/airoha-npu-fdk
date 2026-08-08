/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/compatibility.h"

static void
report_event(const struct npu_wifi_compatibility_diagnostics *diagnostics,
             enum npu_wifi_compatibility_event event) {
  if (diagnostics != NULL && diagnostics->report != NULL)
    diagnostics->report(diagnostics->context, event);
}

void npu_wifi_core7_init_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics) {
  report_event(diagnostics,
               NPU_WIFI_COMPATIBILITY_CORE7_INITIALIZATION_COMPLETE);
}

void npu_wifi_inode_pcie_swap_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t value) {
  (void)value;
  report_event(diagnostics, NPU_WIFI_COMPATIBILITY_INODE_PCIE_SWAP_UNSUPPORTED);
}

void npu_wifi_inode_stop_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface) {
  (void)interface;
  report_event(diagnostics, NPU_WIFI_COMPATIBILITY_INODE_STOP_UNSUPPORTED);
}

void npu_wifi_inode_hardware_config_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface, uint8_t endpoint_mask, uint8_t vap_mask) {
  (void)interface;
  (void)endpoint_mask;
  (void)vap_mask;
  report_event(diagnostics,
               NPU_WIFI_COMPATIBILITY_INODE_HARDWARE_CONFIG_UNSUPPORTED);
}

void npu_wifi_inode_debug_flag_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface, uint32_t debug_flag) {
  (void)interface;
  (void)debug_flag;
  report_event(diagnostics,
               NPU_WIFI_COMPATIBILITY_INODE_DEBUG_FLAG_UNSUPPORTED);
}

uint32_t npu_wifi_wcid_debug_counter_get_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface) {
  (void)interface;
  report_event(diagnostics,
               NPU_WIFI_COMPATIBILITY_WCID_DEBUG_COUNTER_UNSUPPORTED);
  return 0U;
}

bool npu_wifi_ring_size_get_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t direction, uint32_t interface, uint32_t *ring_size) {
  if (ring_size == NULL)
    return false;

  (void)direction;
  (void)interface;
  *ring_size = 0U;
  report_event(diagnostics, NPU_WIFI_COMPATIBILITY_RING_SIZE_UNSUPPORTED);
  return true;
}

bool npu_wifi_ring_dma_address_get_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t direction, uint32_t interface, uint32_t *dma_address) {
  if (dma_address == NULL)
    return false;

  (void)direction;
  (void)interface;
  *dma_address = 0U;
  report_event(diagnostics,
               NPU_WIFI_COMPATIBILITY_RING_DMA_ADDRESS_UNSUPPORTED);
  return true;
}

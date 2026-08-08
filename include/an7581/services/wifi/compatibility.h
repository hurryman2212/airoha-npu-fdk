/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_COMPATIBILITY_H
#define NPU_WIFI_COMPATIBILITY_H

#include "an7581/platform/types.h"

enum npu_wifi_compatibility_event {
  NPU_WIFI_COMPATIBILITY_CORE7_INITIALIZATION_COMPLETE = 0,
  NPU_WIFI_COMPATIBILITY_INODE_PCIE_SWAP_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_INODE_STOP_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_INODE_HARDWARE_CONFIG_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_INODE_DEBUG_FLAG_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_WCID_DEBUG_COUNTER_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_RING_SIZE_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_RING_DMA_ADDRESS_UNSUPPORTED,
  NPU_WIFI_COMPATIBILITY_EVENT_COUNT,
};

typedef void (*npu_wifi_compatibility_report)(
    void *context, enum npu_wifi_compatibility_event event);

struct npu_wifi_compatibility_diagnostics {
  npu_wifi_compatibility_report report;
  void *context;
};

void npu_wifi_core7_init_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics);
void npu_wifi_inode_pcie_swap_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t value);
void npu_wifi_inode_stop_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface);
void npu_wifi_inode_hardware_config_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface, uint8_t endpoint_mask, uint8_t vap_mask);
void npu_wifi_inode_debug_flag_set_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface, uint32_t debug_flag);
uint32_t npu_wifi_wcid_debug_counter_get_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t interface);
bool npu_wifi_ring_size_get_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t direction, uint32_t interface, uint32_t *ring_size);
bool npu_wifi_ring_dma_address_get_wrapper(
    const struct npu_wifi_compatibility_diagnostics *diagnostics,
    uint32_t direction, uint32_t interface, uint32_t *dma_address);
#endif

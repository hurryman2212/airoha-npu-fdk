/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RX_PCIE_H
#define NPU_WIFI_RX_PCIE_H

#include "an7581/platform/types.h"
#include "an7581/services/wifi/mailbox.h"

#define NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET UINT32_C(0x00000008)
#define NPU_WIFI_RX_PCIE_DMA_INDEX_OFFSET UINT32_C(0x0000000c)
#define NPU_WIFI_RX_PCIE_TRIGGER_INTERFACE UINT32_C(15)
#define NPU_WIFI_RX_PCIE_WRITE_LIMIT 5U

struct npu_wifi_rx_pcie_write {
  uint32_t address;
  uint32_t value;
  uint8_t interface;
};

struct npu_wifi_rx_pcie_write_plan {
  struct npu_wifi_rx_pcie_write writes[NPU_WIFI_RX_PCIE_WRITE_LIMIT];
  uint32_t write_count;
};

struct npu_wifi_rx_pcie_state {
  uint32_t register_base[NPU_WIFI_INTERFACE_COUNT];
  uint32_t valid_interfaces;
};

typedef bool (*npu_wifi_rx_pcie_write32)(void *context, uint32_t address,
                                         uint32_t value);

bool npu_wifi_rx_pcie_initialize(struct npu_wifi_rx_pcie_state *state);
bool npu_wifi_rx_pcie_prepare(const struct npu_wifi_rx_pcie_state *state,
                              uint32_t interface, uint32_t register_base,
                              uint32_t tx_done_descriptor_count,
                              bool tx_done_descriptor_count_valid,
                              struct npu_wifi_rx_pcie_write_plan *plan);
bool npu_wifi_rx_pcie_program(const struct npu_wifi_rx_pcie_write_plan *plan,
                              npu_wifi_rx_pcie_write32 write32,
                              void *write_context);
bool npu_wifi_rx_pcie_apply(struct npu_wifi_rx_pcie_state *state,
                            uint32_t interface, uint32_t register_base,
                            uint32_t tx_done_descriptor_count,
                            bool tx_done_descriptor_count_valid,
                            npu_wifi_rx_pcie_write32 write32,
                            void *write_context);

#endif

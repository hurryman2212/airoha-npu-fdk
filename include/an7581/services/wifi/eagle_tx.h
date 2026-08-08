/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_EAGLE_TX_H
#define NPU_WIFI_EAGLE_TX_H

#include "an7581/platform/types.h"
#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_PCIE_PORT_TYPE_COUNT 4U
#define NPU_WIFI_PCIE_TX_WINDOW_START_OFFSET UINT32_C(0x00008030)
#define NPU_WIFI_PCIE_TX_WINDOW_END_OFFSET UINT32_C(0x00008034)
#define NPU_WIFI_PCIE_TX_WINDOW_ADDRESS_MASK UINT32_C(0x1fffffff)

struct npu_wifi_eagle_tx_window {
  uint32_t start_register;
  uint32_t end_register;
  uint32_t start_address;
  uint32_t end_address;
  uint8_t band;
  uint8_t pcie_port_type;
};

typedef bool (*npu_wifi_eagle_tx_write32)(void *context, uint32_t address,
                                          uint32_t value);
typedef bool (*npu_wifi_eagle_tx_read32)(void *context, uint32_t address,
                                         uint32_t *value);
struct npu_wifi_eagle_tx_initialize_context {
  npu_wifi_eagle_tx_write32 write32;
  void *write_context;
  npu_wifi_eagle_tx_read32 read32;
  void *read_context;
  volatile uint16_t *tx_state;
  volatile uint16_t *descriptor_state;
};

bool npu_wifi_eagle_tx_window_plan(uint32_t dynamic_base,
                                   uint32_t pcie_port_type, uint32_t band,
                                   struct npu_wifi_eagle_tx_window *window);
bool npu_wifi_eagle_tx_window_program(
    const struct npu_wifi_eagle_tx_window *window,
    npu_wifi_eagle_tx_write32 write32, void *write_context);
bool npu_wifi_eagle_tx_window_verify(
    const struct npu_wifi_eagle_tx_window *window,
    npu_wifi_eagle_tx_read32 read32, void *read_context);
bool npu_wifi_eagle_tx_window_program_verified(
    const struct npu_wifi_eagle_tx_window *window,
    npu_wifi_eagle_tx_write32 write32, void *write_context,
    npu_wifi_eagle_tx_read32 read32, void *read_context);
bool npu_wifi_eagle_tx_initialize(
    uint32_t dynamic_base, uint32_t pcie_port_type, uint32_t band,
    const struct npu_wifi_eagle_tx_initialize_context *context);

#endif

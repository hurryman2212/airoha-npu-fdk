/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_PACKET_SPACE_H
#define NPU_WIFI_TX_PACKET_SPACE_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/region.h"

#define NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE UINT32_C(0x00000010)
#define NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT UINT32_C(0x00000200)
#define NPU_WIFI_TX_PACKET_BUFFER_STRIDE UINT32_C(0x00000100)
#define NPU_WIFI_TX_PACKET_BUFFER_OWNED UINT32_C(0x80000000)
#define NPU_WIFI_TX_PACKET_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)

struct npu_wifi_tx_packet_descriptor {
  uint32_t buffer_address;
  uint32_t packet_address;
  uint32_t token_control;
  uint32_t status;
};

struct npu_wifi_tx_packet_space_profile {
  uint32_t region_type;
  uint32_t descriptor_count;
  uint32_t descriptor_size;
  uint8_t activation_index;
};

const struct npu_wifi_tx_packet_space_profile *
npu_wifi_tx_packet_space_find_profile(uint32_t activation_index);
bool npu_wifi_tx_packet_space_region_lookup(uint32_t activation_index,
                                            struct npu_wifi_region *region);
enum npu_runtime_result npu_wifi_tx_packet_space_initialize(
    const struct npu_wifi_tx_packet_space_profile *profile,
    uint32_t packet_space_address, void *descriptor_memory,
    size_t descriptor_memory_size);

#endif

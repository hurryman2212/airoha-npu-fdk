/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_BUFFER_SPACE_H
#define NPU_WIFI_TX_BUFFER_SPACE_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/region.h"

#define NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE UINT32_C(0x00000080)
#define NPU_WIFI_TX_BUFFER_SPACE_HEAD_CONTROL_INITIAL_VALUE UINT32_C(0x00100000)
#define NPU_WIFI_TX_BUFFER_SPACE_TAIL_CONTROL_INITIAL_VALUE UINT32_C(0x00010000)
#define NPU_WIFI_TX_BUFFER_SPACE_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)

enum npu_wifi_tx_buffer_space_role {
  NPU_WIFI_TX_BUFFER_SPACE_QUEUE_BASE = 0,
  NPU_WIFI_TX_BUFFER_SPACE_FREE_POINTER_TABLE,
  NPU_WIFI_TX_BUFFER_SPACE_PACKET_BASE,
};

enum npu_wifi_tx_buffer_space_storage {
  NPU_WIFI_TX_BUFFER_SPACE_EXTERNAL = 0,
  NPU_WIFI_TX_BUFFER_SPACE_DYNAMIC_REGION,
  NPU_WIFI_TX_BUFFER_SPACE_FIXED_ADDRESS,
};

struct npu_wifi_tx_buffer_space_record {
  uint32_t hardware_head_control;
  uint32_t hardware_passthrough_words1_to7[7];
  uint32_t token_control;
  uint32_t route_control;
  uint32_t station_control;
  uint32_t hardware_passthrough_word11;
  uint32_t packet_address;
  uint32_t packet_length;
  uint32_t hardware_passthrough_words14_to30[17];
  uint32_t hardware_tail_control;
};

struct npu_wifi_tx_buffer_space_profile {
  enum npu_wifi_tx_buffer_space_role role;
  enum npu_wifi_tx_buffer_space_storage storage;
  uint32_t dynamic_region_type;
  uint32_t fixed_address;
  uint32_t record_count;
  uint32_t record_size;
  uint8_t set_interface;
  uint8_t activation_index;
};

const struct npu_wifi_tx_buffer_space_profile *
npu_wifi_tx_buffer_space_find_profile(uint32_t set_interface);
bool npu_wifi_tx_buffer_space_region_lookup(
    const struct npu_wifi_tx_buffer_space_profile *profile,
    uint32_t dynamic_base, uint32_t host_address,
    struct npu_wifi_region *region);
enum npu_runtime_result npu_wifi_tx_buffer_space_initialize(
    const struct npu_wifi_tx_buffer_space_profile *profile, void *record_memory,
    size_t record_memory_size);

#endif

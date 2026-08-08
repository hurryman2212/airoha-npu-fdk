/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RX_RING_H
#define NPU_WIFI_RX_RING_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define NPU_WIFI_RX_DESCRIPTOR_LIMIT UINT32_C(0x00000600)
#define NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT UINT32_C(0x00000200)
#define NPU_WIFI_RX_MT7996_SECONDARY_DESCRIPTOR_LIMIT UINT32_C(0x00000400)
#define NPU_WIFI_RX_MT7996_MSDU0_DESCRIPTOR_LIMIT UINT32_C(0x00000100)
#define NPU_WIFI_RX_MT7996_MSDU1_DESCRIPTOR_LIMIT UINT32_C(0x00000200)
#define NPU_WIFI_RX_MT7996_MSDU2_DESCRIPTOR_LIMIT UINT32_C(0x00000400)
#define NPU_WIFI_RX_DESCRIPTOR_SIZE UINT32_C(0x00000010)
#define NPU_WIFI_RX_PACKET_BUFFER_SIZE UINT32_C(0x00000800)
#define NPU_WIFI_RX_MSDU_PAGE_SIZE UINT32_C(0x00000080)
#define NPU_WIFI_RX_NO_PUBLICATION_INTERFACE UINT8_MAX

enum npu_wifi_rx_ring_kind {
  NPU_WIFI_RX_RING_EAGLE_DATA = 0,
  NPU_WIFI_RX_RING_MSDU_PAGE,
  NPU_WIFI_RX_RING_INDICATION,
  NPU_WIFI_RX_RING_RXDMAD_C,
  NPU_WIFI_RX_RING_TX_DONE,
  NPU_WIFI_RX_RING_IGNORED,
};

struct npu_wifi_rx_descriptor {
  uint32_t buffer_address;
  uint32_t control;
  uint32_t buffer_id;
  uint32_t sequence_control;
};

struct npu_wifi_rx_ring_profile {
  enum npu_wifi_rx_ring_kind kind;
  uint32_t maximum_descriptor_count;
  uint32_t descriptor_size;
  uint32_t buffer_stride;
  uint32_t packet_data_offset;
  uint32_t initial_control;
  uint8_t set_interface;
  uint8_t publication_interface;
  bool allocates_buffers;
  bool stores_buffer_id;
};

struct npu_wifi_rx_buffer_operations {
  bool (*allocate)(void *context, uint16_t *buffer_id);
  void (*release)(void *context, uint16_t buffer_id);
};

struct npu_wifi_msdu_page_descriptor_config {
  void *descriptor_memory;
  uint16_t *page_ids;
  size_t descriptor_memory_size;
  uint32_t page_pool_base;
  uint32_t page_id_capacity;
  uint32_t set_interface;
  struct npu_wifi_rx_buffer_operations page_id_operations;
  void *operation_context;
};

struct npu_wifi_msdu_page_descriptor_state {
  uint32_t descriptor_count;
  uint8_t set_interface;
  bool ready;
};

const struct npu_wifi_rx_ring_profile *
npu_wifi_rx_ring_find_profile(uint32_t set_interface);
enum npu_runtime_result npu_wifi_rx_ring_initialize(
    const struct npu_wifi_rx_ring_profile *profile, uint32_t packet_buffer_base,
    void *descriptor_memory, size_t descriptor_memory_size,
    uint32_t descriptor_count, uint16_t *buffer_ids,
    uint32_t buffer_id_capacity,
    const struct npu_wifi_rx_buffer_operations *operations, void *context);
enum npu_runtime_result npu_wifi_msdu_page_descriptors_initialize(
    struct npu_wifi_msdu_page_descriptor_state *state,
    const struct npu_wifi_msdu_page_descriptor_config *config,
    uint32_t descriptor_count);

#endif

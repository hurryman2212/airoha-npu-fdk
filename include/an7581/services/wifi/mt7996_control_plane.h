/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_CONTROL_PLANE_H
#define NPU_WIFI_MT7996_CONTROL_PLANE_H

#include "an7581/services/wifi/backend_bundle.h"
#include "an7581/services/wifi/buffer_id_map.h"
#include "an7581/services/wifi/eagle_tx_backend.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"
#include "an7581/services/wifi/packet_id_backend.h"
#include "an7581/services/wifi/rx_backend.h"
#include "an7581/services/wifi/rx_pcie_backend.h"
#include "an7581/services/wifi/tx_backend.h"
#include "an7581/services/wifi/tx_buffer_backend.h"
#include "an7581/services/wifi/tx_done_descriptors.h"
#include "an7581/services/wifi/tx_packet_backend.h"

#define NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT 6U
#define NPU_WIFI_MT7996_CONTROL_TX_ARENA_COUNT 2U
#define NPU_WIFI_MT7996_CONTROL_TX_BUFFER_ARENA_COUNT 2U
#define NPU_WIFI_MT7996_CONTROL_TX_PACKET_ARENA_COUNT 2U
#define NPU_WIFI_MT7996_CONTROL_ADDITIONAL_BACKEND_LIMIT 4U
#define NPU_WIFI_MT7996_CONTROL_PACKET_ID_COUNT UINT32_C(0x00001100)

struct an7581_wifi_tdm_rx_platform;

struct npu_wifi_mt7996_memory_binding {
  void *memory;
  size_t size;
  uint32_t physical_base;
};

typedef bool (*npu_wifi_mt7996_host_buffer_map)(void *context,
                                                uint32_t host_address,
                                                size_t length,
                                                uint32_t alignment,
                                                void **mapped_memory);
struct npu_wifi_mt7996_control_plane_config {
  struct npu_wifi_configuration *configuration;
  struct npu_wifi_sram_allocator *shared_allocator;
  struct npu_wifi_packet_id_pool *shared_packet_pool;
  struct npu_wifi_mt7996_memory_binding packet_recycle;
  struct npu_wifi_mt7996_memory_binding token_ids;
  struct npu_wifi_mt7996_memory_binding force_reset_ids;
  struct npu_wifi_mt7996_memory_binding dynamic_arena;
  struct npu_wifi_mt7996_memory_binding msdu_page_ids;
  struct npu_wifi_mt7996_memory_binding tx_done_packet_ids;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  struct npu_wifi_mt7996_memory_binding
      tx_packet_descriptors[NPU_WIFI_MT7996_CONTROL_TX_PACKET_ARENA_COUNT];
  npu_wifi_packet_id_pool_lock_operation acquire;
  npu_wifi_packet_id_pool_lock_operation release;
  void *lock_context;
  npu_wifi_mt7996_host_buffer_map map_host_buffer;
  void *map_context;
  struct an7581_wifi_tdm_rx_platform *tdm_rx_platform;
  npu_wifi_tx_done_force_reset_operation force_reset_token_ids;
  void *force_reset_context;
  npu_wifi_tx_ring_sram_warning report_tx_descriptor_sram_warning;
  void *tx_descriptor_warning_context;
  npu_wifi_rx_pcie_write32 write32;
  void *write_context;
  npu_wifi_eagle_tx_read32 read32;
  void *read_context;
  const struct npu_wifi_backend_binding *additional_backends;
  size_t additional_backend_count;
  bool activation_allowed;
};

struct npu_wifi_mt7996_control_plane {
  struct npu_wifi_configuration *configuration;
  struct npu_wifi_sram_allocator allocator;
  struct npu_wifi_sram_allocator *allocator_owner;
  struct npu_wifi_region packet_recycle_region;
  struct npu_wifi_region token_region;
  struct npu_wifi_region force_reset_region;
  struct npu_wifi_region dynamic_region;
  struct npu_wifi_packet_id_pool packet_pool;
  struct npu_wifi_packet_id_pool *packet_pool_owner;
  struct npu_wifi_packet_id_backend packet_id_backend;
  struct npu_wifi_buffer_id_map msdu_page_id_pool;
  struct npu_wifi_rx_arena rx_arenas[NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT];
  struct npu_wifi_rx_static_backend rx_backend;
  struct npu_wifi_tx_arena tx_arenas[NPU_WIFI_MT7996_CONTROL_TX_ARENA_COUNT];
  struct npu_wifi_tx_static_backend tx_backend;
  struct npu_wifi_tx_buffer_space_arena
      tx_buffer_arenas[NPU_WIFI_MT7996_CONTROL_TX_BUFFER_ARENA_COUNT];
  struct npu_wifi_tx_buffer_space_backend tx_buffer_backend;
  struct npu_wifi_tx_packet_arena
      tx_packet_arenas[NPU_WIFI_MT7996_CONTROL_TX_PACKET_ARENA_COUNT];
  struct npu_wifi_tx_packet_static_backend tx_packet_backend;
  struct npu_wifi_rx_pcie_backend rx_pcie_backend;
  struct npu_wifi_eagle_tx_backend eagle_tx_backend;
  struct npu_wifi_backend_bundle backend_bundle;
  struct npu_wifi_tx_descriptor_base_state tx_descriptor_base;
  uint16_t packet_ids[NPU_WIFI_MT7996_CONTROL_PACKET_ID_COUNT];
  npu_wifi_mt7996_host_buffer_map map_host_buffer;
  void *map_context;
  void *tx_done_descriptor_memory;
  size_t tx_done_descriptor_memory_size;
  struct npu_wifi_tx_done_descriptor_state tx_done_state;
  uint16_t *tx_done_packet_ids;
  npu_wifi_tx_done_force_reset_operation force_reset_token_ids;
  void *force_reset_context;
  npu_wifi_tx_ring_sram_warning report_tx_descriptor_sram_warning;
  void *tx_descriptor_warning_context;
  uint32_t tx_done_host_address;
  bool tx_done_host_address_valid;
  bool shared_state_external;
  bool activation_gated;
  bool additional_backends_bound;
  bool initialized;
};

enum npu_runtime_result npu_wifi_mt7996_control_plane_initialize(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_mt7996_control_plane_config *config);
enum npu_runtime_result npu_wifi_mt7996_control_plane_bind_backends(
    struct npu_wifi_mt7996_control_plane *control_plane,
    const struct npu_wifi_backend_binding *backends, size_t backend_count);
enum npu_runtime_result npu_wifi_mt7996_control_plane_prepare_reinitialization(
    struct npu_wifi_mt7996_control_plane *control_plane);
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_TDM_RX_H
#define AN7581_WIFI_TDM_RX_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/region.h"
#include "an7581/services/wifi/tdm_rx.h"
#include "an7581/services/wifi/token_id_reset.h"

#define AN7581_WIFI_TDM_RX_DESCRIPTOR_BASE UINT32_C(0x3e891000)
#define AN7581_WIFI_TDM_RX_RING_SPAN UINT32_C(0x8000)
#define AN7581_WIFI_TDM_RX_REGISTERS_BASE UINT32_C(0x1fb50900)
#define AN7581_WIFI_TDM_RX_REGISTER_STRIDE UINT32_C(0x10)
#define AN7581_WIFI_TDM_RX_GLOBAL_CONTROL_ADDRESS UINT32_C(0x1fb54710)
#define AN7581_WIFI_TDM_RX_GLOBAL_ENABLE_ADDRESS UINT32_C(0x1fb50a04)

struct an7581_wifi_tdm_rx_memory {
  volatile struct an7581_qdma_descriptor
      *descriptors[NPU_WIFI_TDM_RX_RING_COUNT];
  volatile struct npu_wifi_tdm_rx_registers
      *registers[NPU_WIFI_TDM_RX_RING_COUNT];
  volatile uint16_t *reset_scratch_entries;
  volatile uint16_t *token_states;
  volatile uint32_t *global_control;
  volatile uint32_t *global_ring_enable;
  uint32_t descriptor_physical_base[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t packet_buffer_base;
};

struct an7581_wifi_tdm_rx_platform_config {
  struct an7581_wifi_tdm_rx_memory memory;
  struct npu_wifi_packet_id_pool *token_pool;
  npu_wifi_tdm_rx_dispatch dispatch;
  npu_wifi_tdm_rx_dispatch_publish publish_dispatch;
  void *dispatch_context;
  struct npu_wifi_tdm_rx_diagnostic_counters
      diagnostic_counters[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t token_state_count;
};

struct an7581_wifi_tdm_rx_platform {
  struct npu_wifi_tdm_rx receiver;
  struct npu_wifi_token_id_reset_config reset;
  uint32_t token_state_count;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_tdm_rx_memory_resolve(
    struct npu_wifi_sram_allocator *allocator,
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_tdm_rx_memory *memory);
enum npu_runtime_result an7581_wifi_tdm_rx_platform_initialize(
    struct an7581_wifi_tdm_rx_platform *platform,
    const struct an7581_wifi_tdm_rx_platform_config *config);
enum npu_runtime_result an7581_wifi_tdm_rx_token_pool_force_reset(
    struct an7581_wifi_tdm_rx_platform *platform);

#endif

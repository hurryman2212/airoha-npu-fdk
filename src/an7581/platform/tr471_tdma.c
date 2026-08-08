/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tr471_tdma.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/region.h"

static bool descriptor_region_resolve(uint32_t type,
                                      uint32_t expected_descriptor_base,
                                      uint32_t *descriptor_base) {
  struct npu_wifi_region region;

  if (descriptor_base == NULL ||
      !npu_wifi_mt7996_fixed_region_lookup(type, &region) ||
      region.address > UINT32_MAX - AN7581_TR471_TDMA_REGION_DESCRIPTOR_OFFSET)
    return false;

  *descriptor_base =
      region.address + AN7581_TR471_TDMA_REGION_DESCRIPTOR_OFFSET;
  return *descriptor_base == expected_descriptor_base;
}

enum npu_runtime_result
an7581_tr471_tdma_memory_resolve(const struct npu_tr471_state *state,
                                 uint32_t shared_buffer_extent,
                                 struct an7581_tr471_tdma_memory *memory) {
  struct an7581_tr471_tdma_memory candidate;
  uint32_t local_address;
  uint32_t rx_descriptor_base;
  uint32_t tx_descriptor_base;

  if (state == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!state->buffer_address_valid ||
      shared_buffer_extent < NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT ||
      !descriptor_region_resolve(
          NPU_WIFI_MT7996_FIXED_TDMA_DELIVERY_DESCRIPTORS,
          AN7581_TR471_TDMA_TX_DESCRIPTOR_BASE, &tx_descriptor_base) ||
      !descriptor_region_resolve(NPU_WIFI_MT7996_FIXED_TDM_RX_DESCRIPTORS,
                                 AN7581_TR471_TDMA_RX_DESCRIPTOR_BASE,
                                 &rx_descriptor_base) ||
      !an7581_dma_buffer_map(state->buffer_address,
                             NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT,
                             NPU_TR471_TDMA_PACKET_BUFFER_SIZE, &local_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(&candidate, 0U, sizeof(candidate));
  candidate.tx_descriptors =
      (volatile struct an7581_qdma_descriptor *)(uintptr_t)tx_descriptor_base;
  candidate.rx_descriptors =
      (volatile struct an7581_qdma_descriptor *)(uintptr_t)rx_descriptor_base;
  candidate.tx_registers =
      (volatile struct npu_tr471_tdma_registers *)(uintptr_t)
          AN7581_TR471_TDMA_TX_REGISTERS;
  candidate.rx_registers =
      (volatile struct npu_tr471_tdma_registers *)(uintptr_t)
          AN7581_TR471_TDMA_RX_REGISTERS;
  candidate.rx_global_control =
      (volatile uint32_t *)(uintptr_t)AN7581_TR471_TDMA_RX_GLOBAL_CONTROL;
  candidate.rx_global_ring_enable =
      (volatile uint32_t *)(uintptr_t)AN7581_TR471_TDMA_RX_GLOBAL_RING_ENABLE;
  candidate.tx_queue_config =
      (volatile uint32_t *)(uintptr_t)AN7581_TR471_TDMA_TX_QUEUE_CONFIG;
  candidate.tx_queue_enable =
      (volatile uint32_t *)(uintptr_t)AN7581_TR471_TDMA_TX_QUEUE_ENABLE;
  candidate.shared_buffers = (uint8_t *)(uintptr_t)local_address;
  candidate.tx_descriptor_dma_base = tx_descriptor_base;
  candidate.rx_descriptor_dma_base = rx_descriptor_base;
  candidate.shared_buffer_dma_base = state->buffer_address;
  candidate.shared_buffer_extent = shared_buffer_extent;
  *memory = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_tr471_tdma_platform_initialize(
    struct an7581_tr471_tdma_platform *platform,
    const struct an7581_tr471_tdma_memory *memory) {
  struct npu_tr471_tdma_config config;
  enum npu_runtime_result status;

  if (platform == NULL || memory == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;

  config = (struct npu_tr471_tdma_config){
      .tx_descriptors = memory->tx_descriptors,
      .rx_descriptors = memory->rx_descriptors,
      .tx_registers = memory->tx_registers,
      .rx_registers = memory->rx_registers,
      .rx_global_control = memory->rx_global_control,
      .rx_global_ring_enable = memory->rx_global_ring_enable,
      .tx_queue_config = memory->tx_queue_config,
      .tx_queue_enable = memory->tx_queue_enable,
      .shared_buffers = memory->shared_buffers,
      .tx_descriptor_dma_base = memory->tx_descriptor_dma_base,
      .rx_descriptor_dma_base = memory->rx_descriptor_dma_base,
      .shared_buffer_dma_base = memory->shared_buffer_dma_base,
      .shared_buffer_extent = memory->shared_buffer_extent,
  };
  status = npu_tr471_tdma_initialize(&platform->tdma, &config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

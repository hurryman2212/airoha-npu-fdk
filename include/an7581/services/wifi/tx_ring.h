/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_RING_H
#define NPU_WIFI_TX_RING_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/region.h"

#define NPU_WIFI_TX_DESCRIPTOR_SIZE UINT32_C(0x00000010)
#define NPU_WIFI_TX_DESCRIPTOR_READY UINT32_C(0x80000000)
#define NPU_WIFI_MT7996_TX_BAND_COUNT UINT32_C(2)
#define NPU_WIFI_MT7996_TX_BAND0_DESCRIPTOR_COUNT UINT32_C(0x200)
#define NPU_WIFI_MT7996_TX_SECONDARY_DESCRIPTOR_COUNT UINT32_C(0x400)
#define NPU_WIFI_TX_DESCRIPTOR_BASE_SLOT_COUNT 2U

struct npu_wifi_tx_descriptor {
  uint32_t buffer0;
  uint32_t control;
  uint32_t buffer1;
  uint32_t information;
};

struct npu_wifi_tx_ring_registers {
  volatile uint32_t descriptor_base;
  volatile uint32_t descriptor_count;
  volatile uint32_t cpu_index;
  volatile uint32_t dma_index;
};

struct npu_wifi_tx_producer_state {
  uint16_t index[NPU_WIFI_MT7996_TX_BAND_COUNT];
};

struct npu_wifi_tx_descriptor_base_state {
  uint32_t local_address[NPU_WIFI_TX_DESCRIPTOR_BASE_SLOT_COUNT];
};

typedef void (*npu_wifi_tx_ring_sram_warning)(void *context);

static inline bool npu_wifi_tx_producer_state_is_valid(
    const struct npu_wifi_tx_producer_state *state) {
  return state != NULL &&
         (uint32_t)state->index[0] <
             NPU_WIFI_MT7996_TX_BAND0_DESCRIPTOR_COUNT &&
         (uint32_t)state->index[1] <
             NPU_WIFI_MT7996_TX_SECONDARY_DESCRIPTOR_COUNT;
}

static inline bool npu_wifi_tx_producer_state_is_valid_for_counts(
    const struct npu_wifi_tx_producer_state *state, uint32_t band0_count,
    uint32_t secondary_count) {
  return state != NULL && band0_count != 0U && secondary_count != 0U &&
         (uint32_t)state->index[0] < band0_count &&
         (uint32_t)state->index[1] < secondary_count;
}

struct npu_wifi_tx_ring_profile {
  uint32_t region_type;
  uint32_t descriptor_count;
  uint32_t descriptor_size;
  uint8_t get_interface;
};

const struct npu_wifi_tx_ring_profile *
npu_wifi_tx_ring_find_profile(uint32_t get_interface);
bool npu_wifi_tx_ring_region_lookup(uint32_t dynamic_base,
                                    uint32_t get_interface,
                                    struct npu_wifi_region *region);
enum npu_runtime_result
npu_wifi_tx_ring_prime(const struct npu_wifi_tx_ring_profile *profile,
                       void *descriptor_memory, size_t descriptor_memory_size);
bool npu_wifi_tx_ring_store_descriptor_base(
    struct npu_wifi_tx_descriptor_base_state *state, uint32_t physical_address,
    uint32_t band);
bool npu_wifi_tx_ring_set_descriptor_base_sram(
    struct npu_wifi_tx_descriptor_base_state *state, uint32_t physical_address,
    uint32_t band, npu_wifi_tx_ring_sram_warning report_warning,
    void *warning_context);

#endif

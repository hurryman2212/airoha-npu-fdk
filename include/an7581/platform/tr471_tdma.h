/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_TDMA_H
#define AN7581_TR471_TDMA_H

#include "an7581/services/tr471/mailbox.h"
#include "an7581/services/tr471/tdma.h"

#define AN7581_TR471_TDMA_REGION_DESCRIPTOR_OFFSET UINT32_C(0x10000)
#define AN7581_TR471_TDMA_TX_DESCRIPTOR_BASE UINT32_C(0x3e890000)
#define AN7581_TR471_TDMA_RX_DESCRIPTOR_BASE UINT32_C(0x3e8a1000)

#define AN7581_TR471_TDMA_TX_REGISTERS UINT32_C(0x1fb50810)
#define AN7581_TR471_TDMA_RX_REGISTERS UINT32_C(0x1fb50920)
#define AN7581_TR471_TDMA_RX_GLOBAL_CONTROL UINT32_C(0x1fb54710)
#define AN7581_TR471_TDMA_RX_GLOBAL_RING_ENABLE UINT32_C(0x1fb50a04)
#define AN7581_TR471_TDMA_TX_QUEUE_CONFIG UINT32_C(0x1fb50a2c)
#define AN7581_TR471_TDMA_TX_QUEUE_ENABLE UINT32_C(0x1fb50a28)

struct an7581_tr471_tdma_memory {
  volatile struct an7581_qdma_descriptor *tx_descriptors;
  volatile struct an7581_qdma_descriptor *rx_descriptors;
  volatile struct npu_tr471_tdma_registers *tx_registers;
  volatile struct npu_tr471_tdma_registers *rx_registers;
  volatile uint32_t *rx_global_control;
  volatile uint32_t *rx_global_ring_enable;
  volatile uint32_t *tx_queue_config;
  volatile uint32_t *tx_queue_enable;
  uint8_t *shared_buffers;
  uint32_t tx_descriptor_dma_base;
  uint32_t rx_descriptor_dma_base;
  uint32_t shared_buffer_dma_base;
  uint32_t shared_buffer_extent;
};

struct an7581_tr471_tdma_platform {
  struct npu_tr471_tdma tdma;
  bool initialized;
};

enum npu_runtime_result
an7581_tr471_tdma_memory_resolve(const struct npu_tr471_state *state,
                                 uint32_t shared_buffer_extent,
                                 struct an7581_tr471_tdma_memory *memory);
enum npu_runtime_result an7581_tr471_tdma_platform_initialize(
    struct an7581_tr471_tdma_platform *platform,
    const struct an7581_tr471_tdma_memory *memory);

#endif

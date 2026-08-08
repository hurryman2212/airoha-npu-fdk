/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_TR471_TDMA_H
#define NPU_TR471_TDMA_H

#include "an7581/platform/qdma.h"
#include "an7581/runtime/status.h"

#define NPU_TR471_TDMA_RING_ENTRY_COUNT UINT32_C(0x80)
#define NPU_TR471_TDMA_DESCRIPTOR_SIZE AN7581_QDMA_DESCRIPTOR_SIZE
#define NPU_TR471_TDMA_PACKET_BUFFER_SIZE UINT32_C(0x800)
#define NPU_TR471_TDMA_PACKET_BUFFER_COUNT UINT32_C(0x100)
#define NPU_TR471_TDMA_TX_BUFFER_COUNT NPU_TR471_TDMA_RING_ENTRY_COUNT
#define NPU_TR471_TDMA_RX_BUFFER_COUNT NPU_TR471_TDMA_RING_ENTRY_COUNT
#define NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT                            \
  (NPU_TR471_TDMA_PACKET_BUFFER_COUNT * NPU_TR471_TDMA_PACKET_BUFFER_SIZE)
#define NPU_TR471_TDMA_TX_PACKET_OFFSET UINT32_C(2)
#define NPU_TR471_TDMA_TX_PACKET_CAPACITY                                      \
  (NPU_TR471_TDMA_PACKET_BUFFER_SIZE - NPU_TR471_TDMA_TX_PACKET_OFFSET)
#define NPU_TR471_TDMA_RX_PACKET_CAPACITY UINT32_C(2000)

#define NPU_TR471_TDMA_DESCRIPTOR_OWNED UINT32_C(0x80000000)
#define NPU_TR471_TDMA_DESCRIPTOR_BIT30 UINT32_C(0x40000000)
#define NPU_TR471_TDMA_DESCRIPTOR_UPPER_PRESERVE_MASK UINT32_C(0x7fff0000)
#define NPU_TR471_TDMA_DESCRIPTOR_LENGTH_MASK UINT32_C(0x0000ffff)
#define NPU_TR471_TDMA_BUFFER_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_TR471_TDMA_BUFFER_DEVICE_ALIAS UINT32_C(0x80000000)
#define NPU_TR471_TDMA_RING_ADDRESS_MASK UINT32_C(0x1fffffff)
#define NPU_TR471_TDMA_RING_COUNT_MASK UINT32_C(0x00001fff)

#define NPU_TR471_TDMA_TX_MESSAGE0 UINT32_C(0x00003000)
#define NPU_TR471_TDMA_TX_MESSAGE1 UINT32_C(0x7f4087ff)
#define NPU_TR471_TDMA_RX_GLOBAL_CONTROL_ENABLE UINT32_C(0x80000000)
#define NPU_TR471_TDMA_RX_GLOBAL_RING_ENABLE UINT32_C(0x00000004)
#define NPU_TR471_TDMA_TX_QUEUE_CONFIG UINT32_C(0x02020202)
#define NPU_TR471_TDMA_TX_QUEUE_ENABLE UINT32_C(0x00000011)

struct npu_tr471_tdma_registers {
  volatile uint32_t descriptor_base;
  volatile uint32_t descriptor_count;
  volatile uint32_t cpu_index;
  volatile uint32_t dma_index;
};

struct npu_tr471_tdma_buffer {
  uint8_t *packet;
  uint32_t device_address;
  uint32_t capacity;
};

struct npu_tr471_tdma_config {
  volatile struct an7581_qdma_descriptor *tx_descriptors;
  volatile struct an7581_qdma_descriptor *rx_descriptors;
  volatile struct npu_tr471_tdma_registers *tx_registers;
  volatile struct npu_tr471_tdma_registers *rx_registers;
  volatile uint32_t *rx_global_control;
  volatile uint32_t *rx_global_ring_enable;
  volatile uint32_t *tx_queue_config;
  volatile uint32_t *tx_queue_enable;
  const struct npu_tr471_tdma_buffer *tx_buffers;
  const struct npu_tr471_tdma_buffer *rx_buffers;
  uint8_t *shared_buffers;
  uint32_t tx_descriptor_dma_base;
  uint32_t rx_descriptor_dma_base;
  uint32_t shared_buffer_dma_base;
  uint32_t shared_buffer_extent;
};

struct npu_tr471_tdma_tx_slot {
  uint8_t *packet;
  uint32_t capacity;
  uint16_t descriptor_index;
};

struct npu_tr471_tdma_rx_packet {
  const uint8_t *packet;
  uint32_t device_address;
  uint16_t length;
  uint16_t descriptor_index;
};

struct npu_tr471_tdma {
  volatile struct an7581_qdma_descriptor *tx_descriptors;
  volatile struct an7581_qdma_descriptor *rx_descriptors;
  volatile struct npu_tr471_tdma_registers *tx_registers;
  volatile struct npu_tr471_tdma_registers *rx_registers;
  volatile uint32_t *rx_global_control;
  volatile uint32_t *rx_global_ring_enable;
  volatile uint32_t *tx_queue_config;
  volatile uint32_t *tx_queue_enable;
  const struct npu_tr471_tdma_buffer *tx_buffers;
  const struct npu_tr471_tdma_buffer *rx_buffers;
  uint8_t *shared_buffers;
  uint32_t tx_descriptor_dma_base;
  uint32_t rx_descriptor_dma_base;
  uint32_t shared_buffer_dma_base;
  uint32_t shared_buffer_extent;
  uint16_t tx_producer;
  uint16_t rx_consumer;
  uint32_t transmitted_packet_count;
  uint32_t received_packet_count;
  uint32_t ownership_error_count;
  bool rx_packet_outstanding;
  bool initialized;
};

enum npu_runtime_result
npu_tr471_tdma_initialize(struct npu_tr471_tdma *tdma,
                          const struct npu_tr471_tdma_config *config);
enum npu_runtime_result
npu_tr471_tdma_tx_take(struct npu_tr471_tdma *tdma,
                       struct npu_tr471_tdma_tx_slot *slot);
enum npu_runtime_result
npu_tr471_tdma_tx_submit(struct npu_tr471_tdma *tdma,
                         const struct npu_tr471_tdma_tx_slot *slot,
                         uint32_t packet_length, uint32_t message1);
enum npu_runtime_result
npu_tr471_tdma_rx_take(struct npu_tr471_tdma *tdma,
                       struct npu_tr471_tdma_rx_packet *packet);
enum npu_runtime_result
npu_tr471_tdma_rx_release(struct npu_tr471_tdma *tdma,
                          const struct npu_tr471_tdma_rx_packet *packet);
enum npu_runtime_result npu_tr471_tdma_tx_reset(struct npu_tr471_tdma *tdma);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TDM_RX_H
#define NPU_WIFI_TDM_RX_H

#include "an7581/platform/qdma.h"
#include "an7581/services/wifi/packet_id_pool.h"

#define NPU_WIFI_TDM_RX_RING_COUNT UINT32_C(2)
#define NPU_WIFI_TDM_RX_RING_ENTRY_COUNT UINT32_C(0x400)
#define NPU_WIFI_TDM_RX_DESCRIPTOR_SIZE AN7581_QDMA_DESCRIPTOR_SIZE
#define NPU_WIFI_TDM_RX_PACKET_SIZE UINT32_C(0x800)
#define NPU_WIFI_TDM_RX_BATCH_LIMIT UINT32_C(0x80)
#define NPU_WIFI_TDM_RX_PUBLISH_INTERVAL UINT32_C(8)
#define NPU_WIFI_TDM_RX_DESCRIPTOR_OWNED UINT32_C(0x80000000)
#define NPU_WIFI_TDM_RX_DESCRIPTOR_LENGTH_MASK UINT32_C(0x0000ffff)
#define NPU_WIFI_TDM_RX_DESCRIPTOR_PRESERVE_MASK UINT32_C(0x7fff0000)
#define NPU_WIFI_TDM_RX_BUFFER_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_TDM_RX_BUFFER_DEVICE_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_TDM_RX_RING_ADDRESS_MASK UINT32_C(0x1fffffff)
#define NPU_WIFI_TDM_RX_GLOBAL_CONTROL_ENABLE UINT32_C(0x80000000)
#define NPU_WIFI_TDM_RX_GLOBAL_RING_ENABLE UINT32_C(0x00000004)

struct npu_wifi_tdm_rx_registers {
  volatile uint32_t descriptor_base;
  volatile uint32_t descriptor_count;
  volatile uint32_t cpu_index;
  volatile uint32_t dma_index;
};

struct npu_wifi_tdm_rx_packet {
  uint32_t buffer_address;
  uint32_t message[4];
  uint16_t token_id;
  uint16_t length;
};

typedef enum npu_runtime_result (*npu_wifi_tdm_rx_dispatch)(
    void *context, uint32_t ring_index,
    const struct npu_wifi_tdm_rx_packet *packet);
typedef enum npu_runtime_result (*npu_wifi_tdm_rx_dispatch_publish)(
    void *context);

struct npu_wifi_tdm_rx_diagnostic_counters {
  volatile uint32_t *descriptors_consumed;
  volatile uint32_t *token_allocation_failures;
};

struct npu_wifi_tdm_rx_config {
  volatile struct an7581_qdma_descriptor
      *descriptors[NPU_WIFI_TDM_RX_RING_COUNT];
  volatile struct npu_wifi_tdm_rx_registers
      *registers[NPU_WIFI_TDM_RX_RING_COUNT];
  volatile uint32_t *global_control;
  volatile uint32_t *global_ring_enable;
  struct npu_wifi_packet_id_pool *token_pool;
  npu_wifi_tdm_rx_dispatch dispatch;
  npu_wifi_tdm_rx_dispatch_publish publish_dispatch;
  void *dispatch_context;
  struct npu_wifi_tdm_rx_diagnostic_counters
      diagnostic_counters[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t descriptor_physical_base[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t packet_buffer_base;
};

struct npu_wifi_tdm_rx {
  volatile struct an7581_qdma_descriptor
      *descriptors[NPU_WIFI_TDM_RX_RING_COUNT];
  volatile struct npu_wifi_tdm_rx_registers
      *registers[NPU_WIFI_TDM_RX_RING_COUNT];
  volatile uint32_t *global_control;
  volatile uint32_t *global_ring_enable;
  struct npu_wifi_packet_id_pool *token_pool;
  npu_wifi_tdm_rx_dispatch dispatch;
  npu_wifi_tdm_rx_dispatch_publish publish_dispatch;
  void *dispatch_context;
  struct npu_wifi_tdm_rx_diagnostic_counters
      diagnostic_counters[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t descriptor_physical_base[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t packet_buffer_base;
  uint16_t consumer[NPU_WIFI_TDM_RX_RING_COUNT];
  uint32_t initialized_descriptor_count;
  uint32_t consumed_descriptor_count;
  uint32_t dispatched_packet_count;
  uint32_t dropped_packet_count;
  uint32_t allocation_failure_count;
  uint32_t dispatch_failure_count;
  uint32_t cpu_index_publish_count;
  bool initialized;
};

enum npu_runtime_result
npu_wifi_tdm_rx_initialize(struct npu_wifi_tdm_rx *receiver,
                           const struct npu_wifi_tdm_rx_config *config);
enum npu_runtime_result
npu_wifi_tdm_rx_consume(struct npu_wifi_tdm_rx *receiver, uint32_t ring_index,
                        uint32_t descriptor_limit, uint32_t *processed_count);

#endif

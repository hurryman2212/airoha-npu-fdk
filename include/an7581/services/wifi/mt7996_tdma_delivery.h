/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_TDMA_DELIVERY_H
#define NPU_WIFI_MT7996_TDMA_DELIVERY_H

#include "an7581/platform/qdma.h"
#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/mt7996_packet_control.h"
#include "an7581/services/wifi/tx_ring.h"

#define NPU_WIFI_MT7996_TDMA_BAND_COUNT UINT32_C(3)
#define NPU_WIFI_MT7996_TDMA_ALL_BANDS_MASK UINT32_C(0x7)
#define NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT UINT32_C(0x800)
#define NPU_WIFI_MT7996_TDMA_DESCRIPTOR_SIZE AN7581_QDMA_DESCRIPTOR_SIZE
#define NPU_WIFI_MT7996_TDMA_MINIMUM_FREE UINT32_C(10)
#define NPU_WIFI_MT7996_TDMA_CAPACITY_ATTEMPTS UINT32_C(5)
#define NPU_WIFI_MT7996_TDMA_CAPACITY_DELAY_ITERATIONS UINT32_C(300)
#define NPU_WIFI_MT7996_TDMA_MINIMUM_FRAME_LENGTH UINT32_C(60)
#define NPU_WIFI_MT7996_TDMA_PACKET_ID_SHIFT UINT32_C(14)
#define NPU_WIFI_MT7996_TDMA_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_MT7996_TDMA_RRO_PACKET_STRIDE                                 \
  NPU_WIFI_MT7996_PACKET_CONTROL_PACKET_STRIDE
#define NPU_WIFI_MT7996_TDMA_RRO_PAYLOAD_OFFSET UINT32_C(0x80)

struct npu_wifi_mt7996_tdma_band_config {
  volatile struct an7581_qdma_descriptor *descriptors;
  volatile struct npu_wifi_tx_ring_registers *registers;
  volatile uint32_t *full_observation_counter;
  volatile uint32_t *capacity_timeout_counter;
  volatile uint32_t *published_counter;
  uint32_t producer;
};

struct npu_wifi_mt7996_tdma_delivery_config {
  struct npu_wifi_mt7996_tdma_band_config band[NPU_WIFI_MT7996_TDMA_BAND_COUNT];
  volatile uint8_t *rro_packet_mapping;
  npu_wifi_mt7996_packet_queue_enqueue_callback enqueue;
  npu_wifi_rro_cpu_queue_release release;
  void *packet_context;
  size_t rro_packet_mapping_size;
  uint32_t rro_packet_dma_base;
  uint32_t rro_packet_count;
  uint32_t enabled_band_mask;
};

struct npu_wifi_mt7996_tdma_statistics {
  uint32_t full_observation_count;
  uint32_t capacity_timeout_count;
  uint32_t published_count;
};

struct npu_wifi_mt7996_tdma_delivery {
  struct npu_wifi_mt7996_tdma_band_config band[NPU_WIFI_MT7996_TDMA_BAND_COUNT];
  struct npu_wifi_mt7996_tdma_statistics
      statistics[NPU_WIFI_MT7996_TDMA_BAND_COUNT];
  struct npu_wifi_mt7996_packet_control packet_control;
  uint32_t rro_packet_dma_base;
  uint32_t enabled_band_mask;
  bool initialized;
};

struct npu_wifi_mt7996_tdma_request {
  volatile uint8_t *packet_data;
  size_t packet_data_size;
  uint32_t packet_id;
  uint32_t packet_address;
  uint16_t length;
  uint8_t band;
};

enum npu_runtime_result npu_wifi_mt7996_tdma_delivery_initialize(
    struct npu_wifi_mt7996_tdma_delivery *delivery,
    const struct npu_wifi_mt7996_tdma_delivery_config *config);
enum npu_runtime_result npu_wifi_mt7996_tdma_delivery_publish(
    struct npu_wifi_mt7996_tdma_delivery *delivery,
    const struct npu_wifi_mt7996_tdma_request *request);
extern const struct npu_wifi_rro_cpu_queue_operations
    npu_wifi_mt7996_tdma_rro_operations;

#endif

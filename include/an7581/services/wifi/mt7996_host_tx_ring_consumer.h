/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_HOST_TX_RING_CONSUMER_H
#define NPU_WIFI_MT7996_HOST_TX_RING_CONSUMER_H

#include "an7581/services/wifi/tx_packet_space.h"
#include "an7581/services/wifi/tx_ring.h"

#define NPU_WIFI_MT7996_HOST_TX_BAND_COUNT UINT32_C(2)
#define NPU_WIFI_MT7996_HOST_TX_DESCRIPTOR_SIZE UINT32_C(0xd0)
#define NPU_WIFI_MT7996_HOST_TX_TXWI_OFFSET UINT32_C(0x10)
#define NPU_WIFI_MT7996_HOST_TX_TXWI_SIZE UINT32_C(0xc0)
#define NPU_WIFI_MT7996_HOST_TX_TXWI_COPY_SIZE UINT32_C(0x4c)
#define NPU_WIFI_MT7996_HOST_TX_BATCH_LIMIT UINT32_C(0x80)
#define NPU_WIFI_MT7996_HOST_TX_RING_COUNT_LIMIT UINT32_C(0x10000)
#define NPU_WIFI_MT7996_HOST_TX_READY UINT32_C(1)
#define NPU_WIFI_MT7996_HOST_TX_PACKET_TOKEN_SHIFT UINT32_C(18)

struct npu_wifi_mt7996_host_tx_descriptor {
  uint32_t control;
  uint32_t packet_address;
  uint64_t reserved;
  uint8_t txwi[NPU_WIFI_MT7996_HOST_TX_TXWI_SIZE];
};

struct npu_wifi_mt7996_host_tx_diagnostic_counters {
  volatile uint32_t *descriptor_attempts;
  volatile uint32_t *destination_full;
  volatile uint32_t *budget_exhaustions;
};

typedef enum npu_runtime_result (*npu_wifi_mt7996_host_tx_map_ring)(
    void *context, uint32_t physical_address, size_t size,
    volatile struct npu_wifi_mt7996_host_tx_descriptor **descriptors);
typedef enum npu_runtime_result (*npu_wifi_mt7996_host_tx_transfer_txwi)(
    void *context, const volatile uint8_t *source_txwi,
    uint32_t destination_address, uint16_t *record_token);

struct npu_wifi_mt7996_host_tx_ring_consumer_band_config {
  volatile struct npu_wifi_tx_ring_registers *host_ring;
  volatile struct npu_wifi_tx_packet_descriptor *destination_descriptors;
  struct npu_wifi_mt7996_host_tx_diagnostic_counters diagnostic_counters;
};

struct npu_wifi_mt7996_host_tx_ring_consumer_config {
  struct npu_wifi_mt7996_host_tx_ring_consumer_band_config
      band[NPU_WIFI_MT7996_HOST_TX_BAND_COUNT];
  npu_wifi_mt7996_host_tx_map_ring map_ring;
  npu_wifi_mt7996_host_tx_transfer_txwi transfer_txwi;
  void *operation_context;
  uint32_t destination_descriptor_count;
};

struct npu_wifi_mt7996_host_tx_ring_consumer {
  struct npu_wifi_mt7996_host_tx_ring_consumer_band_config
      band[NPU_WIFI_MT7996_HOST_TX_BAND_COUNT];
  npu_wifi_mt7996_host_tx_map_ring map_ring;
  npu_wifi_mt7996_host_tx_transfer_txwi transfer_txwi;
  void *operation_context;
  uint32_t destination_descriptor_count;
  uint32_t staged_descriptor_count;
  uint32_t destination_full_count;
  uint32_t transfer_failure_count;
  uint16_t host_consumer[NPU_WIFI_MT7996_HOST_TX_BAND_COUNT];
  uint16_t destination_producer[NPU_WIFI_MT7996_HOST_TX_BAND_COUNT];
  bool initialized;
};

struct npu_wifi_mt7996_host_tx_ring_consumer_result {
  uint32_t processed;
  uint16_t next_host_consumer;
  uint16_t next_destination_producer;
  bool stopped_on_empty;
  bool stopped_on_destination_full;
  bool budget_exhausted;
  bool pending_work;
};

enum npu_runtime_result npu_wifi_mt7996_host_tx_ring_consumer_initialize(
    struct npu_wifi_mt7996_host_tx_ring_consumer *consumer,
    const struct npu_wifi_mt7996_host_tx_ring_consumer_config *config);
enum npu_runtime_result npu_wifi_mt7996_host_tx_ring_consume(
    struct npu_wifi_mt7996_host_tx_ring_consumer *consumer, uint32_t band,
    uint32_t budget,
    struct npu_wifi_mt7996_host_tx_ring_consumer_result *result);

#endif

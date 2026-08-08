/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_MT7996_HOST_RX_H
#define NPU_WIFI_MT7996_HOST_RX_H

#include "an7581/services/wifi/mt7996_packet_queue_consumer.h"

#define NPU_WIFI_MT7996_HOST_RX_ENTRY_COUNT UINT32_C(0x200)
#define NPU_WIFI_MT7996_HOST_RX_ENTRY_SIZE UINT32_C(0x18)
#define NPU_WIFI_MT7996_HOST_RX_COPY_LIMIT UINT32_C(0x700)
#define NPU_WIFI_MT7996_HOST_RX_PACKET_LENGTH_LIMIT UINT32_C(0x3fff)

struct npu_wifi_mt7996_host_rx_descriptor {
  uint32_t control;
  uint32_t metadata;
  uint32_t packet_word;
  uint32_t destination_address;
  uint32_t host_private[2];
};

typedef enum npu_runtime_result (*npu_wifi_mt7996_host_rx_copy)(
    void *context, uint32_t source_address, uint32_t destination_address,
    uint32_t length);

struct npu_wifi_mt7996_host_rx_diagnostic_counters {
  volatile uint32_t *enqueue_attempts;
  volatile uint32_t *ring_full;
  volatile uint32_t *fallback_length_uses;
  volatile uint32_t *normal_length_uses;
  volatile uint32_t *descriptors_built;
};

struct npu_wifi_mt7996_host_rx_config {
  volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptors;
  volatile uint32_t *producer;
  npu_wifi_mt7996_host_rx_copy copy;
  void *copy_context;
  struct npu_wifi_mt7996_host_rx_diagnostic_counters diagnostic_counters;
  size_t descriptor_memory_size;
};

struct npu_wifi_mt7996_host_rx {
  volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptors;
  volatile uint32_t *producer;
  npu_wifi_mt7996_host_rx_copy copy;
  void *copy_context;
  struct npu_wifi_mt7996_host_rx_diagnostic_counters diagnostic_counters;
  uint32_t enqueue_count;
  uint32_t full_count;
  uint32_t normal_length_count;
  uint32_t oversize_length_count;
  uint32_t copy_failure_count;
  bool initialized;
};

enum npu_runtime_result npu_wifi_mt7996_host_rx_initialize(
    struct npu_wifi_mt7996_host_rx *host_rx,
    const struct npu_wifi_mt7996_host_rx_config *config);
enum npu_runtime_result npu_wifi_mt7996_host_rx_enqueue(
    void *context, const struct npu_wifi_mt7996_packet_delivery *delivery);
enum npu_runtime_result npu_wifi_mt7996_host_rx_rebind_memory(
    struct npu_wifi_mt7996_host_rx *host_rx,
    volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptors,
    volatile uint32_t *producer, size_t descriptor_memory_size);

#endif

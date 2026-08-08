/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_HOST_RX_H
#define AN7581_WIFI_MT7996_HOST_RX_H

#include "an7581/services/wifi/mt7996_host_rx.h"

#define AN7581_WIFI_MT7996_HOST_RX_BAND_COUNT UINT32_C(2)
#define AN7581_WIFI_MT7996_HOST_RX_RING0_BASE_CONTROL UINT32_C(0x1ec0d180)
#define AN7581_WIFI_MT7996_HOST_RX_RING0_PRODUCER UINT32_C(0x1ec0d188)
#define AN7581_WIFI_MT7996_HOST_RX_RING1_BASE_CONTROL UINT32_C(0x1ec0d190)
#define AN7581_WIFI_MT7996_HOST_RX_RING1_PRODUCER UINT32_C(0x1ec0d198)
#define AN7581_WIFI_MT7996_HOST_RX_VDMA_CHANNEL UINT32_C(3)

struct an7581_wifi_mt7996_host_rx_memory {
  volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptors;
  volatile uint32_t *producer;
  size_t descriptor_memory_size;
  uint32_t ring_physical_base;
};

struct an7581_wifi_mt7996_host_rx_config {
  struct an7581_wifi_mt7996_host_rx_memory memory;
  struct npu_wifi_mt7996_host_rx_diagnostic_counters diagnostic_counters;
  uint32_t vdma_poll_limit;
};

struct an7581_wifi_mt7996_host_rx_platform {
  struct npu_wifi_mt7996_host_rx service;
  uint32_t vdma_poll_limit;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_host_rx_memory_resolve(
    uint32_t band, struct an7581_wifi_mt7996_host_rx_memory *memory);
enum npu_runtime_result an7581_wifi_mt7996_host_rx_platform_initialize(
    struct an7581_wifi_mt7996_host_rx_platform *platform,
    const struct an7581_wifi_mt7996_host_rx_config *config);

#endif

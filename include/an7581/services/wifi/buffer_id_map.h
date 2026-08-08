/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_BUFFER_ID_MAP_H
#define NPU_WIFI_BUFFER_ID_MAP_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT UINT32_C(0x2000)

struct npu_wifi_buffer_id_map {
  volatile uint16_t *entries;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  volatile uint16_t consumer_index;
  volatile uint16_t producer_index;
  enum npu_runtime_result last_allocation_status;
  enum npu_runtime_result last_release_status;
  uint32_t allocation_count;
  uint32_t release_count;
  uint32_t allocation_failure_count;
  uint32_t release_failure_count;
  uint32_t reset_count;
  bool initialized;
};

enum npu_runtime_result npu_wifi_buffer_id_map_initialize(
    struct npu_wifi_buffer_id_map *map, volatile uint16_t *entries,
    uint32_t entry_count,
    volatile struct npu_wifi_mt7996_band2_diagnostic_counters
        *diagnostic_counters);
enum npu_runtime_result
npu_wifi_buffer_id_map_allocate(struct npu_wifi_buffer_id_map *map,
                                uint16_t *buffer_id);
enum npu_runtime_result
npu_wifi_buffer_id_map_release(struct npu_wifi_buffer_id_map *map,
                               uint16_t buffer_id);
enum npu_runtime_result npu_wifi_buffer_id_map_reset(void *context);

extern const struct npu_wifi_rx_buffer_operations
    npu_wifi_buffer_id_map_operations;

#endif

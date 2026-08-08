/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_PAGE_BACKEND_H
#define NPU_WIFI_RRO_PAGE_BACKEND_H

#include "an7581/services/wifi/rro_page.h"

#define NPU_WIFI_RRO_PAGE_RELEASE_QUEUE_SIZE UINT32_C(8192)
#define NPU_WIFI_RRO_RECORD_ALIAS_BIT UINT32_C(0x80000000)
#define NPU_WIFI_RRO_PHYSICAL_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_RRO_PAGE_POOL_ADDRESS_LIMIT UINT32_C(0xbfffffff)

struct npu_wifi_rro_page_backend {
  volatile uint8_t *record_mapping;
  volatile uint8_t *trailer_mapping;
  volatile uint16_t *release_queue;
  volatile uint32_t *release_counter;
  npu_wifi_rro_cache_discard discard;
  npu_wifi_rro_record_consume consume;
  void *discard_context;
  void *consume_context;
  uint32_t page_pool_base;
  uint32_t page_count;
  uint16_t release_producer;
};

enum npu_runtime_result npu_wifi_rro_page_backend_initialize(
    struct npu_wifi_rro_page_backend *backend, uint32_t page_pool_base,
    uint32_t page_count, volatile void *record_mapping,
    volatile void *trailer_mapping, volatile uint16_t *release_queue,
    volatile uint32_t *release_counter, npu_wifi_rro_cache_discard discard,
    void *discard_context, npu_wifi_rro_record_consume consume,
    void *consume_context);

extern const struct npu_wifi_rro_page_operations
    npu_wifi_rro_page_backend_operations;

#endif

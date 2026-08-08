/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_PAGE_H
#define NPU_WIFI_RRO_PAGE_H

#include "an7581/services/wifi/rro_metadata.h"

#define NPU_WIFI_RRO_METADATA_RECORDS_PER_PAGE UINT32_C(5)
#define NPU_WIFI_RRO_METADATA_PAGE_SIZE UINT32_C(0x80)
#define NPU_WIFI_RRO_SECOND_CACHE_LINE_OFFSET UINT32_C(0x48)

struct npu_wifi_rro_metadata_page {
  struct npu_wifi_rro_metadata_record
      records[NPU_WIFI_RRO_METADATA_RECORDS_PER_PAGE];
  uint32_t next_page_address;
  int32_t readiness;
};

struct npu_wifi_rro_metadata_page_view {
  const volatile struct npu_wifi_rro_metadata_record *records;
  const volatile uint32_t *next_page_address;
  const volatile int32_t *readiness;
};

struct npu_wifi_rro_page_state {
  volatile uint32_t *delay_counter;
  uint32_t page_pool_base;
  uint32_t page_pool_count;
  uint32_t current_page_address;
  uint32_t pending_next_page_address;
  uint32_t total_record_count;
  uint32_t record_index;
  uint16_t page_slot;
  uint16_t pending_release_id;
  bool pending_release;
};

struct npu_wifi_rro_page_result {
  uint32_t consumed_count;
  uint32_t release_count;
  uint32_t current_page_address;
  uint32_t record_index;
  uint16_t page_slot;
  bool page_not_ready;
  bool complete;
};

typedef enum npu_runtime_result (*npu_wifi_rro_page_map)(
    void *context, uint32_t page_address,
    struct npu_wifi_rro_metadata_page_view *view);
typedef enum npu_runtime_result (*npu_wifi_rro_cache_discard)(
    void *context, uint32_t line_address);
typedef enum npu_runtime_result (*npu_wifi_rro_record_consume)(
    void *context, const struct npu_wifi_rro_metadata_record *record,
    uint32_t record_index, uint32_t page_address, uint16_t page_slot);
typedef enum npu_runtime_result (*npu_wifi_rro_page_release)(void *context,
                                                             uint16_t page_id);

struct npu_wifi_rro_page_operations {
  npu_wifi_rro_page_map map;
  npu_wifi_rro_cache_discard discard;
  npu_wifi_rro_record_consume consume;
  npu_wifi_rro_page_release release;
};

enum npu_runtime_result npu_wifi_rro_page_initialize(
    struct npu_wifi_rro_page_state *state, uint32_t first_page_address,
    uint32_t page_pool_base, uint32_t page_pool_count, uint32_t record_count,
    volatile uint32_t *delay_counter);
enum npu_runtime_result npu_wifi_rro_page_process(
    struct npu_wifi_rro_page_state *state, uint32_t record_budget,
    const struct npu_wifi_rro_page_operations *operations, void *context,
    struct npu_wifi_rro_page_result *result);

#endif

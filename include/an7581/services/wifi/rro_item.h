/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_ITEM_H
#define NPU_WIFI_RRO_ITEM_H

#include "an7581/services/wifi/rro_page.h"
#include "an7581/services/wifi/rro_table.h"

enum npu_wifi_rro_item_phase {
  NPU_WIFI_RRO_ITEM_IDLE = 0,
  NPU_WIFI_RRO_ITEM_CLAIM_TABLE,
  NPU_WIFI_RRO_ITEM_PROCESS_PAGE,
  NPU_WIFI_RRO_ITEM_COMPLETE_TABLE,
};

struct npu_wifi_rro_item_state {
  struct npu_wifi_rro_table_state table_state;
  struct npu_wifi_rro_page_state page_state;
  struct npu_wifi_rro_indication_descriptor descriptor;
  struct npu_wifi_rro_metadata_cursor cursor;
  volatile uint32_t *metadata_page_delay_counter;
  uint32_t page_pool_base;
  uint32_t page_pool_count;
  uint32_t item_offset;
  enum npu_wifi_rro_item_phase phase;
};

struct npu_wifi_rro_item_result {
  struct npu_wifi_rro_metadata_cursor cursor;
  uint32_t records_consumed;
  uint32_t pages_released;
  bool table_retry_performed;
  bool table_skipped;
  bool item_complete;
};

struct npu_wifi_rro_item_operations {
  struct npu_wifi_rro_table_operations table;
  struct npu_wifi_rro_page_operations page;
};

struct npu_wifi_rro_item_contexts {
  void *table;
  void *page;
};

enum npu_runtime_result
npu_wifi_rro_item_initialize(struct npu_wifi_rro_item_state *state,
                             uint32_t page_pool_base, uint32_t page_pool_count,
                             volatile uint32_t *generation_mismatch_counter,
                             volatile uint32_t *metadata_page_delay_counter);
enum npu_runtime_result npu_wifi_rro_item_process(
    struct npu_wifi_rro_item_state *state,
    const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t item_offset, uint32_t record_budget,
    const struct npu_wifi_rro_item_operations *operations,
    const struct npu_wifi_rro_item_contexts *contexts,
    struct npu_wifi_rro_item_result *result);

#endif

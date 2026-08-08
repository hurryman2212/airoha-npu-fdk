/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_DESCRIPTOR_H
#define NPU_WIFI_RRO_DESCRIPTOR_H

#include "an7581/services/wifi/rro_item.h"

typedef enum npu_runtime_result (*npu_wifi_rro_cursor_publish)(
    void *context, uint32_t cursor_value);
typedef enum npu_runtime_result (*npu_wifi_rro_descriptor_prepare)(
    void *context, const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t descriptor_index);

struct npu_wifi_rro_descriptor_service {
  struct npu_wifi_rro_item_state item_state;
  struct npu_wifi_rro_item_operations operations;
  struct npu_wifi_rro_item_contexts contexts;
  struct npu_wifi_rro_item_result last_item_result;
  struct npu_wifi_rro_indication_descriptor descriptor;
  npu_wifi_rro_cursor_publish publish_cursor;
  npu_wifi_rro_descriptor_prepare prepare_descriptor;
  void *publish_context;
  void *prepare_context;
  uint32_t descriptor_index;
  uint32_t item_offset;
  uint32_t item_budget;
  uint32_t record_budget;
  uint32_t committed_descriptor_count;
  uint32_t committed_item_count;
  uint32_t cursor_publication_count;
  uint32_t pending_cursor_value;
  bool active;
  bool descriptor_prepare_pending;
  bool cursor_publication_pending;
};

enum npu_runtime_result npu_wifi_rro_descriptor_initialize(
    struct npu_wifi_rro_descriptor_service *service, uint32_t page_pool_base,
    uint32_t page_pool_count, uint32_t item_budget, uint32_t record_budget,
    volatile uint32_t *generation_mismatch_counter,
    volatile uint32_t *metadata_page_delay_counter,
    const struct npu_wifi_rro_item_operations *operations,
    const struct npu_wifi_rro_item_contexts *contexts,
    npu_wifi_rro_cursor_publish publish_cursor, void *publish_context);
enum npu_runtime_result npu_wifi_rro_descriptor_set_prepare(
    struct npu_wifi_rro_descriptor_service *service,
    npu_wifi_rro_descriptor_prepare prepare_descriptor, void *prepare_context);
enum npu_runtime_result npu_wifi_rro_descriptor_consume(
    void *context, const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t descriptor_index);

#endif

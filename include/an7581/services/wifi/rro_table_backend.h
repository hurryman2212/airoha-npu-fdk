/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_TABLE_BACKEND_H
#define NPU_WIFI_RRO_TABLE_BACKEND_H

#include "an7581/services/wifi/rro_table.h"

#define NPU_WIFI_RRO_NORMAL_TABLE_GROUP_LIMIT UINT32_C(512)
#define NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT UINT32_C(8192)
#define NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT UINT32_C(1024)

struct npu_wifi_rro_table_backend {
  volatile struct npu_wifi_rro_metadata_table_entry **normal_groups;
  volatile struct npu_wifi_rro_metadata_table_entry *special_table;
  uint32_t normal_group_count;
  uint32_t normal_entry_count;
  uint32_t special_entry_count;
};

enum npu_runtime_result npu_wifi_rro_table_backend_initialize(
    struct npu_wifi_rro_table_backend *backend,
    volatile struct npu_wifi_rro_metadata_table_entry **normal_groups,
    uint32_t normal_group_count, uint32_t normal_entry_count,
    volatile struct npu_wifi_rro_metadata_table_entry *special_table,
    uint32_t special_entry_count);
enum npu_runtime_result npu_wifi_rro_table_backend_prepare_entries(
    volatile struct npu_wifi_rro_metadata_table_entry *entries,
    uint32_t entry_count);
enum npu_runtime_result npu_wifi_rro_table_backend_set_normal_group(
    struct npu_wifi_rro_table_backend *backend, uint32_t group,
    volatile struct npu_wifi_rro_metadata_table_entry *entries);
enum npu_runtime_result npu_wifi_rro_table_backend_set_special_table(
    struct npu_wifi_rro_table_backend *backend,
    volatile struct npu_wifi_rro_metadata_table_entry *entries);
enum npu_runtime_result npu_wifi_rro_table_backend_invalidate_selector(
    struct npu_wifi_rro_table_backend *backend, uint32_t table_selector);

extern const struct npu_wifi_rro_table_operations
    npu_wifi_rro_table_backend_operations;

#endif

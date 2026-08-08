/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_TABLE_H
#define NPU_WIFI_RRO_TABLE_H

#include "an7581/services/wifi/rro_metadata.h"

enum npu_wifi_rro_table_phase {
  NPU_WIFI_RRO_TABLE_IDLE = 0,
  NPU_WIFI_RRO_TABLE_READ_INITIAL,
  NPU_WIFI_RRO_TABLE_DELAY_RETRY,
  NPU_WIFI_RRO_TABLE_READ_RETRY,
  NPU_WIFI_RRO_TABLE_INVALIDATE_MISMATCH,
  NPU_WIFI_RRO_TABLE_CLAIMED,
  NPU_WIFI_RRO_TABLE_INVALIDATE_CLAIM,
};

struct npu_wifi_rro_table_state {
  struct npu_wifi_rro_metadata_cursor cursor;
  struct npu_wifi_rro_metadata_table_entry_fields entry;
  volatile uint32_t *generation_mismatch_counter;
  enum npu_wifi_rro_table_phase phase;
  bool retry_performed;
};

struct npu_wifi_rro_table_result {
  struct npu_wifi_rro_metadata_table_entry_fields entry;
  bool retry_performed;
  bool claimed;
  bool skipped;
  bool invalidated;
};

typedef enum npu_runtime_result (*npu_wifi_rro_table_read)(
    void *context, const struct npu_wifi_rro_metadata_cursor *cursor,
    struct npu_wifi_rro_metadata_table_entry *entry);
typedef enum npu_runtime_result (*npu_wifi_rro_table_retry_delay)(
    void *context);
typedef enum npu_runtime_result (*npu_wifi_rro_table_invalidate)(
    void *context, const struct npu_wifi_rro_metadata_cursor *cursor);

struct npu_wifi_rro_table_operations {
  npu_wifi_rro_table_read read;
  npu_wifi_rro_table_retry_delay delay_retry;
  npu_wifi_rro_table_invalidate invalidate;
};

void npu_wifi_rro_table_initialize(
    struct npu_wifi_rro_table_state *state,
    volatile uint32_t *generation_mismatch_counter);
enum npu_runtime_result
npu_wifi_rro_table_claim(struct npu_wifi_rro_table_state *state,
                         const struct npu_wifi_rro_metadata_cursor *cursor,
                         const struct npu_wifi_rro_table_operations *operations,
                         void *context,
                         struct npu_wifi_rro_table_result *result);
enum npu_runtime_result npu_wifi_rro_table_complete(
    struct npu_wifi_rro_table_state *state,
    const struct npu_wifi_rro_table_operations *operations, void *context);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_table_backend.h"

#include "an7581/runtime/memory.h"

#define NPU_WIFI_RRO_TABLE_RETRY_ITERATIONS UINT32_C(100)
#define NPU_WIFI_RRO_TABLE_GENERATION_BYTE_OFFSET UINT32_C(7)

enum npu_runtime_result npu_wifi_rro_table_backend_initialize(
    struct npu_wifi_rro_table_backend *backend,
    volatile struct npu_wifi_rro_metadata_table_entry **normal_groups,
    uint32_t normal_group_count, uint32_t normal_entry_count,
    volatile struct npu_wifi_rro_metadata_table_entry *special_table,
    uint32_t special_entry_count) {
  if (backend == NULL || normal_groups == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)normal_groups & (sizeof(void *) - 1U)) != 0U ||
      (special_table != NULL &&
       ((uintptr_t)special_table & (sizeof(uint32_t) - 1U)) != 0U) ||
      normal_group_count == 0U ||
      normal_group_count > NPU_WIFI_RRO_NORMAL_TABLE_GROUP_LIMIT ||
      normal_entry_count == 0U ||
      normal_entry_count > NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT ||
      special_entry_count == 0U ||
      special_entry_count > NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(backend, 0U, sizeof(*backend));
  backend->normal_groups = normal_groups;
  backend->special_table = special_table;
  backend->normal_group_count = normal_group_count;
  backend->normal_entry_count = normal_entry_count;
  backend->special_entry_count = special_entry_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_table_backend_prepare_entries(
    volatile struct npu_wifi_rro_metadata_table_entry *entries,
    uint32_t entry_count) {
  uint32_t index;

  if (entries == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)entries & (sizeof(uint32_t) - 1U)) != 0U ||
      entry_count == 0U || entry_count > NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < entry_count; ++index) {
    entries[index].page_address = 0U;
    entries[index].control = UINT32_C(0xff000000);
  }
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_table_backend_set_normal_group(
    struct npu_wifi_rro_table_backend *backend, uint32_t group,
    volatile struct npu_wifi_rro_metadata_table_entry *entries) {
  if (backend == NULL || entries == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (backend->normal_groups == NULL || group >= backend->normal_group_count ||
      ((uintptr_t)entries & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  __asm__ volatile("" ::: "memory");
  backend->normal_groups[group] = entries;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_table_backend_set_special_table(
    struct npu_wifi_rro_table_backend *backend,
    volatile struct npu_wifi_rro_metadata_table_entry *entries) {
  if (backend == NULL || entries == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)entries & (sizeof(uint32_t) - 1U)) != 0U ||
      backend->special_entry_count == 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  __asm__ volatile("" ::: "memory");
  backend->special_table = entries;
  return NPU_RUNTIME_SUCCESS;
}

static bool
cursor_is_consistent(const struct npu_wifi_rro_metadata_cursor *cursor) {
  uint32_t expected_index;

  if (cursor->table_selector > NPU_WIFI_RRO_TABLE_SELECTOR_MASK ||
      cursor->table_group != cursor->table_selector >> 3U ||
      cursor->table_slot > NPU_WIFI_RRO_TABLE_SLOT_MASK ||
      cursor->generation > 3U ||
      cursor->uses_special_table !=
          (cursor->table_selector == NPU_WIFI_RRO_SPECIAL_TABLE_SELECTOR))
    return false;
  expected_index =
      ((uint32_t)(cursor->table_selector & 7U) << 10U) | cursor->table_slot;
  return cursor->table_entry_index == expected_index;
}

static enum npu_runtime_result
resolve_entry(struct npu_wifi_rro_table_backend *backend,
              const struct npu_wifi_rro_metadata_cursor *cursor,
              volatile struct npu_wifi_rro_metadata_table_entry **entry) {
  volatile struct npu_wifi_rro_metadata_table_entry *table;
  uint32_t entry_index;

  if (backend == NULL || cursor == NULL || entry == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!cursor_is_consistent(cursor))
    return NPU_RUNTIME_OUT_OF_RANGE;

  if (cursor->uses_special_table) {
    table = backend->special_table;
    entry_index = cursor->table_slot;
    if (table == NULL || ((uintptr_t)table & (sizeof(uint32_t) - 1U)) != 0U ||
        entry_index >= backend->special_entry_count)
      return NPU_RUNTIME_OUT_OF_RANGE;
  } else {
    if (cursor->table_group >= backend->normal_group_count)
      return NPU_RUNTIME_OUT_OF_RANGE;
    table = backend->normal_groups[cursor->table_group];
    entry_index = cursor->table_entry_index;
    if (table == NULL || ((uintptr_t)table & (sizeof(uint32_t) - 1U)) != 0U ||
        entry_index >= backend->normal_entry_count)
      return NPU_RUNTIME_OUT_OF_RANGE;
  }

  *entry = &table[entry_index];
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_table_backend_invalidate_selector(
    struct npu_wifi_rro_table_backend *backend, uint32_t table_selector) {
  volatile struct npu_wifi_rro_metadata_table_entry *table;
  uint32_t first_entry;
  uint32_t index;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (table_selector > NPU_WIFI_RRO_TABLE_SELECTOR_MASK)
    return NPU_RUNTIME_OUT_OF_RANGE;

  if (table_selector == NPU_WIFI_RRO_SPECIAL_TABLE_SELECTOR) {
    table = backend->special_table;
    first_entry = 0U;
    if (table == NULL ||
        backend->special_entry_count < NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT)
      return NPU_RUNTIME_OUT_OF_RANGE;
  } else {
    uint32_t group = table_selector >> 3U;

    first_entry = (table_selector & 7U) << 10U;
    if (backend->normal_groups == NULL ||
        group >= backend->normal_group_count ||
        backend->normal_entry_count < NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT ||
        first_entry > backend->normal_entry_count -
                          NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT)
      return NPU_RUNTIME_OUT_OF_RANGE;
    table = backend->normal_groups[group];
    if (table == NULL)
      return NPU_RUNTIME_OUT_OF_RANGE;
  }
  if (((uintptr_t)table & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT; ++index) {
    volatile uint8_t *bytes = (volatile uint8_t *)&table[first_entry + index];

    bytes[NPU_WIFI_RRO_TABLE_GENERATION_BYTE_OFFSET] = UINT8_C(0xff);
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
read_entry(void *context, const struct npu_wifi_rro_metadata_cursor *cursor,
           struct npu_wifi_rro_metadata_table_entry *entry) {
  struct npu_wifi_rro_table_backend *backend = context;
  volatile struct npu_wifi_rro_metadata_table_entry *source;
  enum npu_runtime_result status;

  status = resolve_entry(backend, cursor, &source);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  entry->page_address = source->page_address;
  entry->control = source->control;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result retry_delay(void *context) {
  volatile uint32_t iteration;

  (void)context;
  for (iteration = 0U; iteration < NPU_WIFI_RRO_TABLE_RETRY_ITERATIONS;
       ++iteration)
    __asm__ volatile("" ::: "memory");
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
invalidate_entry(void *context,
                 const struct npu_wifi_rro_metadata_cursor *cursor) {
  struct npu_wifi_rro_table_backend *backend = context;
  volatile struct npu_wifi_rro_metadata_table_entry *entry;
  volatile uint8_t *bytes;
  enum npu_runtime_result status;

  status = resolve_entry(backend, cursor, &entry);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  bytes = (volatile uint8_t *)entry;
  bytes[NPU_WIFI_RRO_TABLE_GENERATION_BYTE_OFFSET] = UINT8_C(0xff);
  return NPU_RUNTIME_SUCCESS;
}

const struct npu_wifi_rro_table_operations
    npu_wifi_rro_table_backend_operations = {
        .read = read_entry,
        .delay_retry = retry_delay,
        .invalidate = invalidate_entry,
};

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_METADATA_H
#define NPU_WIFI_RRO_METADATA_H

#include "an7581/services/wifi/rro_indication.h"

#define NPU_WIFI_RRO_INDICATION_ITEM_COUNT_MASK UINT32_C(0x00001fff)
#define NPU_WIFI_RRO_METADATA_RECORD_COUNT_LIMIT UINT32_C(0x000007ff)
#define NPU_WIFI_RRO_TABLE_SELECTOR_MASK UINT32_C(0x00000fff)
#define NPU_WIFI_RRO_TABLE_SLOT_MASK UINT32_C(0x000003ff)
#define NPU_WIFI_RRO_SPECIAL_TABLE_SELECTOR UINT32_C(0x00000400)

struct npu_wifi_rro_metadata_cursor {
  uint16_t table_selector;
  uint16_t table_group;
  uint16_t table_entry_index;
  uint16_t table_slot;
  uint8_t generation;
  bool uses_special_table;
};

struct npu_wifi_rro_metadata_record {
  /* MT7996 RRO HIF descriptor data0 through data5, in hardware order. */
  uint32_t data0;
  uint32_t data1;
  uint32_t data2;
  uint32_t data3;
  uint32_t data4;
  uint32_t data5;
};

struct npu_wifi_rro_metadata_record_fields {
  uint32_t packet_control;
  uint16_t buffer_id;
  int16_t signed_buffer_id;
  uint16_t packet_length;
  uint8_t fragment_type;
  uint8_t special_data_offset_units;
  bool last_segment;
  bool packet_control_flag;
};

struct npu_wifi_rro_metadata_table_entry {
  uint32_t page_address;
  uint32_t control;
};

enum npu_wifi_rro_metadata_entry_state {
  NPU_WIFI_RRO_METADATA_ENTRY_MATCHED = 0,
  NPU_WIFI_RRO_METADATA_ENTRY_RETRY_SENTINEL,
  NPU_WIFI_RRO_METADATA_ENTRY_GENERATION_MISMATCH,
};

struct npu_wifi_rro_metadata_table_entry_fields {
  uint32_t page_address;
  uint16_t record_count;
  uint8_t generation;
  enum npu_wifi_rro_metadata_entry_state state;
};

uint32_t npu_wifi_rro_indication_item_count(
    const struct npu_wifi_rro_indication_descriptor *descriptor);
enum npu_runtime_result npu_wifi_rro_metadata_cursor_decode(
    const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t item_offset, struct npu_wifi_rro_metadata_cursor *cursor);
enum npu_runtime_result npu_wifi_rro_metadata_record_decode(
    const struct npu_wifi_rro_metadata_record *record,
    struct npu_wifi_rro_metadata_record_fields *fields);
enum npu_runtime_result npu_wifi_rro_metadata_table_entry_decode(
    const struct npu_wifi_rro_metadata_table_entry *entry,
    const struct npu_wifi_rro_metadata_cursor *cursor,
    struct npu_wifi_rro_metadata_table_entry_fields *fields);

#endif

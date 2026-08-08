/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_metadata.h"

#define NPU_WIFI_RRO_CURSOR_SHIFT UINT32_C(16)
#define NPU_WIFI_RRO_TABLE_GROUP_SHIFT UINT32_C(3)
#define NPU_WIFI_RRO_TABLE_INDEX_SHIFT UINT32_C(10)
#define NPU_WIFI_RRO_PACKET_LENGTH_SHIFT UINT32_C(16)
#define NPU_WIFI_RRO_PACKET_LENGTH_MASK UINT32_C(0x3fff)
#define NPU_WIFI_RRO_FRAGMENT_TYPE_SHIFT UINT32_C(14)
#define NPU_WIFI_RRO_FRAGMENT_TYPE_MASK UINT32_C(0x3)
#define NPU_WIFI_RRO_SPECIAL_DATA_OFFSET_SHIFT UINT32_C(4)
#define NPU_WIFI_RRO_SPECIAL_DATA_OFFSET_MASK UINT32_C(0x7f)
#define NPU_WIFI_RRO_LAST_SEGMENT UINT32_C(0x40000000)
#define NPU_WIFI_RRO_RECORD_FLAG_BIT25 UINT32_C(0x02000000)
#define NPU_WIFI_RRO_PACKET_CONTROL_LENGTH_SHIFT UINT32_C(3)
#define NPU_WIFI_RRO_PACKET_CONTROL_RECORD_FLAG UINT32_C(4)
#define NPU_WIFI_RRO_PACKET_CONTROL_TYPE_FLAG UINT32_C(2)
#define NPU_WIFI_RRO_PACKET_CONTROL_VALUE_SHIFT UINT32_C(17)
#define NPU_WIFI_RRO_PACKET_CONTROL_VALUE_CLEAR_MASK UINT32_C(0xff01ffff)
#define NPU_WIFI_RRO_TABLE_RECORD_COUNT_SHIFT UINT32_C(4)
#define NPU_WIFI_RRO_TABLE_RECORD_COUNT_MASK UINT32_C(0x7ff)
#define NPU_WIFI_RRO_TABLE_GENERATION_SHIFT UINT32_C(24)
#define NPU_WIFI_RRO_TABLE_RETRY_GENERATION UINT32_C(0xff)

_Static_assert(sizeof(struct npu_wifi_rro_metadata_record) == 24U,
               "Wi-Fi RRO metadata record layout changed");
_Static_assert(sizeof(struct npu_wifi_rro_metadata_table_entry) == 8U,
               "Wi-Fi RRO metadata table entry layout changed");

uint32_t npu_wifi_rro_indication_item_count(
    const struct npu_wifi_rro_indication_descriptor *descriptor) {
  if (descriptor == NULL)
    return 0U;
  return descriptor->count_control & NPU_WIFI_RRO_INDICATION_ITEM_COUNT_MASK;
}

enum npu_runtime_result npu_wifi_rro_metadata_cursor_decode(
    const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t item_offset, struct npu_wifi_rro_metadata_cursor *cursor) {
  uint32_t table_selector;
  uint32_t cursor_value;
  uint32_t table_slot;

  if (descriptor == NULL || cursor == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (item_offset >= npu_wifi_rro_indication_item_count(descriptor))
    return NPU_RUNTIME_OUT_OF_RANGE;

  table_selector =
      descriptor->sequence_control & NPU_WIFI_RRO_TABLE_SELECTOR_MASK;
  cursor_value = (((descriptor->sequence_control >> NPU_WIFI_RRO_CURSOR_SHIFT) &
                   NPU_WIFI_RRO_TABLE_SELECTOR_MASK) +
                  item_offset) &
                 NPU_WIFI_RRO_TABLE_SELECTOR_MASK;
  table_slot = cursor_value & NPU_WIFI_RRO_TABLE_SLOT_MASK;

  cursor->table_selector = (uint16_t)table_selector;
  cursor->table_group =
      (uint16_t)(table_selector >> NPU_WIFI_RRO_TABLE_GROUP_SHIFT);
  cursor->table_entry_index = (uint16_t)(((table_selector & UINT32_C(7))
                                          << NPU_WIFI_RRO_TABLE_INDEX_SHIFT) |
                                         table_slot);
  cursor->table_slot = (uint16_t)table_slot;
  cursor->generation =
      (uint8_t)(cursor_value >> NPU_WIFI_RRO_TABLE_INDEX_SHIFT);
  cursor->uses_special_table =
      table_selector == NPU_WIFI_RRO_SPECIAL_TABLE_SELECTOR;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_metadata_record_decode(
    const struct npu_wifi_rro_metadata_record *record,
    struct npu_wifi_rro_metadata_record_fields *fields) {
  uint32_t packet_control;
  uint32_t packet_length;
  uint32_t fragment_type;
  uint32_t special_data_offset_units;

  if (record == NULL || fields == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  packet_length = (record->data1 >> NPU_WIFI_RRO_PACKET_LENGTH_SHIFT) &
                  NPU_WIFI_RRO_PACKET_LENGTH_MASK;
  fragment_type = (record->data1 >> NPU_WIFI_RRO_FRAGMENT_TYPE_SHIFT) &
                  NPU_WIFI_RRO_FRAGMENT_TYPE_MASK;
  special_data_offset_units =
      (record->data1 >> NPU_WIFI_RRO_SPECIAL_DATA_OFFSET_SHIFT) &
      NPU_WIFI_RRO_SPECIAL_DATA_OFFSET_MASK;
  packet_control =
      (((record->data1 >> 30U) ^ 1U) & 1U) |
      (packet_length << NPU_WIFI_RRO_PACKET_CONTROL_LENGTH_SHIFT) |
      ((record->data3 >> 23U) & NPU_WIFI_RRO_PACKET_CONTROL_RECORD_FLAG);
  if (fragment_type == 1U) {
    packet_control |= NPU_WIFI_RRO_PACKET_CONTROL_TYPE_FLAG;
    packet_control &= NPU_WIFI_RRO_PACKET_CONTROL_VALUE_CLEAR_MASK;
    packet_control |= special_data_offset_units
                      << NPU_WIFI_RRO_PACKET_CONTROL_VALUE_SHIFT;
  }

  fields->packet_control = packet_control;
  fields->buffer_id = (uint16_t)record->data4;
  fields->signed_buffer_id = (int16_t)record->data4;
  fields->packet_length = (uint16_t)packet_length;
  fields->fragment_type = (uint8_t)fragment_type;
  fields->special_data_offset_units = (uint8_t)special_data_offset_units;
  fields->last_segment = (record->data1 & NPU_WIFI_RRO_LAST_SEGMENT) != 0U;
  fields->packet_control_flag =
      (record->data3 & NPU_WIFI_RRO_RECORD_FLAG_BIT25) != 0U;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_metadata_table_entry_decode(
    const struct npu_wifi_rro_metadata_table_entry *entry,
    const struct npu_wifi_rro_metadata_cursor *cursor,
    struct npu_wifi_rro_metadata_table_entry_fields *fields) {
  uint32_t generation;

  if (entry == NULL || cursor == NULL || fields == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (cursor->generation > 3U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  generation = entry->control >> NPU_WIFI_RRO_TABLE_GENERATION_SHIFT;
  fields->page_address = entry->page_address;
  fields->record_count =
      (uint16_t)((entry->control >> NPU_WIFI_RRO_TABLE_RECORD_COUNT_SHIFT) &
                 NPU_WIFI_RRO_TABLE_RECORD_COUNT_MASK);
  fields->generation = (uint8_t)generation;
  if (generation == (uint32_t)cursor->generation)
    fields->state = NPU_WIFI_RRO_METADATA_ENTRY_MATCHED;
  else if (generation == NPU_WIFI_RRO_TABLE_RETRY_GENERATION)
    fields->state = NPU_WIFI_RRO_METADATA_ENTRY_RETRY_SENTINEL;
  else
    fields->state = NPU_WIFI_RRO_METADATA_ENTRY_GENERATION_MISMATCH;
  return NPU_RUNTIME_SUCCESS;
}

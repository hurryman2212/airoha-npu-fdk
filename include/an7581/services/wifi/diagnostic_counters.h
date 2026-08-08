/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_DIAGNOSTIC_COUNTERS_H
#define NPU_WIFI_DIAGNOSTIC_COUNTERS_H

#include "an7581/platform/types.h"

/*
 * Reserved spans preserve the vendor shared-memory ABI. The recovered MT7996
 * call graph has no reachable producer for them. The descriptor-classification
 * fields document writes found only in two unreachable vendor helper routines;
 * those helpers are intentionally absent from this firmware.
 */
struct npu_wifi_mt7996_band0_diagnostic_counters {
  uint32_t reserved_000;
  uint32_t rx_refill_eagle_cycles;
  uint32_t rx_packet_id_allocations;
  uint32_t reserved_00c;
  uint32_t rx_packet_id_allocation_failures;
  uint32_t packet_queue_entries_retired;
  uint32_t packet_queue_entries_enqueued;
  uint32_t packet_queue_full;
  uint8_t reserved_020_023[UINT32_C(0x04)];
  uint32_t packet_queue_consume_attempts;
  uint32_t packet_queue_invalid_packet_ids;
  uint32_t packet_queue_zero_lengths;
  uint32_t packet_queue_packets_forwarded;
  uint8_t reserved_034_04b[UINT32_C(0x18)];
  uint32_t reserved_descriptor_classification_rejections;
  uint32_t reserved_050;
  uint32_t tdm_rx_token_allocation_failures;
  uint8_t reserved_058_06b[UINT32_C(0x14)];
  uint32_t packet_queue_forward_failures;
  uint8_t reserved_070_08b[UINT32_C(0x1c)];
  uint32_t host_rx_enqueue_attempts;
  uint8_t reserved_090_0eb[UINT32_C(0x5c)];
  uint32_t host_rx_ring_full;
  uint8_t reserved_0f0_0fb[UINT32_C(0x0c)];
  uint32_t tdma_full_observations;
  uint32_t tdma_capacity_timeouts;
  uint32_t tdm_rx_descriptors_consumed;
  uint32_t reserved_descriptor_classification_flag_rejections;
  uint32_t reserved_10c;
  uint32_t rro_cpu_queue_full_waits;
  uint8_t reserved_114_133[UINT32_C(0x20)];
  uint32_t rro_cpu_queue_entries_enqueued;
  uint32_t rro_cpu_queue_entries_processed;
  uint32_t tdma_descriptors_published;
  uint32_t rro_cpu_queue_normal_entries;
  uint32_t msdu_page_refills_band0;
  uint32_t msdu_page_refills_band1;
  uint32_t msdu_page_refills_band2;
  uint32_t rro_metadata_pages_delayed;
  uint32_t host_tx_descriptor_attempts;
  uint32_t tdm_tx_current_descriptor_waits;
  uint32_t tx_done_records_processed;
  uint32_t host_tx_destination_full;
  uint32_t tx_packet_waits_or_publish_failures;
  uint32_t host_tx_budget_exhaustions;
  uint8_t reserved_16c_173[UINT32_C(0x08)];
  uint32_t tx_done_invalid_record_types;
  uint32_t reserved_178;
  uint32_t tx_packet_descriptor_publish_retries;
  uint32_t tdm_tx_descriptor_publish_retries;
  uint32_t tx_packet_lookahead_descriptor_waits;
  uint32_t tdm_tx_lookahead_descriptor_waits;
  uint8_t reserved_18c_3e7[UINT32_C(0x25c)];
};

struct npu_wifi_mt7996_band1_diagnostic_counters {
  uint32_t reserved_000;
  uint32_t rx_refill_msdu_cycles;
  uint32_t rx_packet_id_allocations;
  uint32_t reserved_00c;
  uint32_t rx_packet_id_allocation_failures;
  uint32_t packet_queue_entries_retired;
  uint32_t packet_queue_entries_enqueued;
  uint32_t packet_queue_full;
  uint32_t rro_packet_queue_releases;
  uint32_t packet_queue_consume_attempts;
  uint32_t packet_queue_invalid_packet_ids;
  uint32_t packet_queue_zero_lengths;
  uint32_t packet_queue_packets_forwarded;
  uint8_t reserved_034_04b[UINT32_C(0x18)];
  uint32_t reserved_descriptor_classification_rejections;
  uint32_t reserved_050;
  uint32_t tdm_rx_token_allocation_failures;
  uint8_t reserved_058_06b[UINT32_C(0x14)];
  uint32_t packet_queue_forward_failures;
  uint8_t reserved_070_087[UINT32_C(0x18)];
  uint32_t fragment_initial_forward_failures;
  uint32_t host_rx_enqueue_attempts;
  uint32_t reserved_090;
  uint32_t fragment_entries_enqueued;
  uint32_t fragment_queue_empty_observations;
  uint32_t fragment_entry_poll_attempts;
  uint32_t fragment_entries_collected;
  uint32_t fragment_sequences_validated;
  uint32_t fragment_end_markers;
  uint32_t fragment_end_count_mismatches;
  uint32_t fragment_end_index_mismatches;
  uint32_t fragment_count_overruns;
  uint32_t fragment_collection_timeouts;
  uint32_t fragments_forwarded;
  uint32_t fragment_entries_retired;
  uint8_t reserved_0c4_0eb[UINT32_C(0x28)];
  uint32_t host_rx_ring_full;
  uint8_t reserved_0f0_103[UINT32_C(0x14)];
  uint32_t tdm_rx_descriptors_consumed;
  uint32_t reserved_descriptor_classification_flag_rejections;
  uint8_t reserved_10c_143[UINT32_C(0x38)];
  uint32_t rro_indication_attempts;
  uint32_t rro_indication_descriptors_available;
  uint32_t rro_table_generation_mismatches;
  uint32_t rro_routed_records;
  uint32_t host_tx_descriptor_attempts;
  uint32_t tdm_tx_current_descriptor_waits;
  uint32_t tx_done_records_processed;
  uint32_t host_tx_destination_full;
  uint32_t tx_packet_waits_or_publish_failures;
  uint32_t host_tx_budget_exhaustions;
  uint8_t reserved_16c_173[UINT32_C(0x08)];
  uint32_t tx_done_invalid_record_types;
  uint32_t reserved_178;
  uint32_t tx_packet_descriptor_publish_retries;
  uint32_t tdm_tx_descriptor_publish_retries;
  uint32_t tx_packet_lookahead_descriptor_waits;
  uint32_t tdm_tx_lookahead_descriptor_waits;
  uint8_t reserved_18c_3e7[UINT32_C(0x25c)];
};

/*
 * The compact band-2 block is the shared accounting target used by the
 * packet, token, and MSDU-page ID pools.
 */
struct npu_wifi_mt7996_band2_diagnostic_counters {
  uint32_t reserved_000;
  uint32_t completion_release_only_records;
  uint32_t completion_dispatch_attempts;
  uint32_t completion_dispatch_failure_releases;
  uint32_t reserved_10;
  uint32_t reserved_14;
  uint32_t packet_id_releases;
  uint32_t packet_id_allocations;
  uint32_t packet_id_allocation_failures;
  uint32_t reserved_24;
  uint32_t reserved_28;
  uint32_t reserved_2c;
  uint32_t host_rx_fallback_length_uses;
  uint32_t host_rx_descriptor_length_uses;
  uint32_t host_rx_descriptors_built;
  uint32_t reserved_3c;
  uint32_t rro_metadata_pages_released;
  uint32_t msdu_page_id_allocation_failures;
  uint32_t msdu_page_id_allocations;
  uint32_t token_id_releases;
  uint32_t token_id_allocations;
  uint32_t token_id_allocation_failures;
  uint32_t completion_type_0x56_records;
  uint32_t completion_type_0x1fa_records;
  uint32_t completion_type_0x5e8_records;
  uint32_t reserved_64;
  uint32_t invalid_completion_token_ids;
  uint32_t completion_token_release_attempts;
  uint32_t reserved_70;
  uint32_t reserved_74;
};

_Static_assert(sizeof(struct npu_wifi_mt7996_band0_diagnostic_counters) ==
                   UINT32_C(0x3e8),
               "MT7996 band-0 diagnostic counter layout changed");
_Static_assert(sizeof(struct npu_wifi_mt7996_band1_diagnostic_counters) ==
                   UINT32_C(0x3e8),
               "MT7996 band-1 diagnostic counter layout changed");
_Static_assert(sizeof(struct npu_wifi_mt7996_band2_diagnostic_counters) ==
                   UINT32_C(0x78),
               "MT7996 band-2 diagnostic counter layout changed");

#endif

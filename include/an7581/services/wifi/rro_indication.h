/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_INDICATION_H
#define NPU_WIFI_RRO_INDICATION_H

#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT NPU_WIFI_RX_DESCRIPTOR_LIMIT
#define NPU_WIFI_RRO_INDICATION_DESCRIPTOR_SIZE UINT32_C(8)
#define NPU_WIFI_RRO_INDICATION_PUBLICATION_INTERVAL UINT32_C(128)

/*
 * MT7996 RRO indication-command layout.  sequence_control carries the
 * indication reason, starting sequence number, and session ID.  count_control
 * carries the ring generation (magic counter) and indication count.
 */
struct npu_wifi_rro_indication_descriptor {
  uint32_t sequence_control;
  uint32_t count_control;
};

struct npu_wifi_rro_indication_state {
  volatile struct npu_wifi_rro_indication_descriptor *descriptors;
  volatile uint32_t *available_counter;
  uint32_t register_base;
  uint32_t descriptor_count;
  uint32_t consumer_index;
  uint32_t last_consumed_index;
  uint32_t pending_publication_count;
  uint32_t forced_publication_count;
  uint8_t expected_sequence;
  bool last_consumed_index_valid;
};

struct npu_wifi_rro_indication_result {
  uint32_t consumed_count;
  uint32_t publication_count;
  uint32_t consumer_index;
  uint8_t expected_sequence;
  bool unavailable;
};

typedef enum npu_runtime_result (*npu_wifi_rro_indication_consume)(
    void *context, const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t descriptor_index);
typedef bool (*npu_wifi_rro_indication_write32)(void *context, uint32_t address,
                                                uint32_t value);

enum npu_runtime_result npu_wifi_rro_indication_initialize(
    struct npu_wifi_rro_indication_state *state, void *descriptor_memory,
    size_t descriptor_memory_size, uint32_t descriptor_count,
    uint32_t register_base, volatile uint32_t *available_counter);
enum npu_runtime_result npu_wifi_rro_indication_process(
    struct npu_wifi_rro_indication_state *state, uint32_t consume_budget,
    npu_wifi_rro_indication_consume consume, void *consume_context,
    npu_wifi_rro_indication_write32 write32, void *write_context,
    struct npu_wifi_rro_indication_result *result);
enum npu_runtime_result npu_wifi_rro_indication_flush_cpu_index(
    struct npu_wifi_rro_indication_state *state,
    npu_wifi_rro_indication_write32 write32, void *write_context);

#endif

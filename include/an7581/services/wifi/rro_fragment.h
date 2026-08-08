/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_FRAGMENT_H
#define NPU_WIFI_RRO_FRAGMENT_H

#include "an7581/services/wifi/rro_metadata.h"

#define NPU_WIFI_RRO_FRAGMENT_LIMIT UINT32_C(7)
#define NPU_WIFI_RRO_FRAGMENT_ACTION_LIMIT UINT32_C(8)

enum npu_wifi_rro_fragment_action_type {
  NPU_WIFI_RRO_FRAGMENT_WRITE_CONTROL = 0,
  NPU_WIFI_RRO_FRAGMENT_DISPATCH,
  NPU_WIFI_RRO_FRAGMENT_RELEASE,
};

struct npu_wifi_rro_fragment {
  uint16_t buffer_id;
  uint16_t length;
};

struct npu_wifi_rro_fragment_action {
  enum npu_wifi_rro_fragment_action_type type;
  uint16_t buffer_id;
  uint16_t total_length;
  uint16_t fragment_length;
  uint8_t flags;
  uint32_t packet_control;
};

struct npu_wifi_rro_fragment_state {
  struct npu_wifi_rro_fragment fragments[NPU_WIFI_RRO_FRAGMENT_LIMIT];
  struct npu_wifi_rro_fragment_action
      actions[NPU_WIFI_RRO_FRAGMENT_ACTION_LIMIT];
  struct npu_wifi_rro_metadata_record_fields pending_record;
  uint32_t first_packet_control;
  uint32_t total_length;
  uint32_t rejected_dispatch_count;
  uint8_t fragment_count;
  uint8_t action_count;
  uint8_t action_index;
  bool active;
  bool discarding;
  bool record_pending;
};

struct npu_wifi_rro_fragment_result {
  uint32_t actions_completed;
  uint32_t dispatch_rejections;
  uint8_t fragment_count;
  bool record_committed;
  bool active;
  bool discarding;
};

typedef enum npu_runtime_result (*npu_wifi_rro_fragment_control_write)(
    void *context, uint16_t buffer_id, uint32_t packet_control);
typedef enum npu_runtime_result (*npu_wifi_rro_fragment_dispatch)(
    void *context, int16_t buffer_id, uint16_t total_length, uint8_t flags,
    uint16_t fragment_length);
typedef enum npu_runtime_result (*npu_wifi_rro_fragment_release)(
    void *context, uint16_t buffer_id);

struct npu_wifi_rro_fragment_operations {
  npu_wifi_rro_fragment_control_write write_control;
  npu_wifi_rro_fragment_dispatch dispatch;
  npu_wifi_rro_fragment_release release;
};

void npu_wifi_rro_fragment_initialize(
    struct npu_wifi_rro_fragment_state *state);
enum npu_runtime_result npu_wifi_rro_fragment_consume(
    struct npu_wifi_rro_fragment_state *state,
    const struct npu_wifi_rro_metadata_record_fields *record,
    const struct npu_wifi_rro_fragment_operations *operations, void *context,
    struct npu_wifi_rro_fragment_result *result);

#endif

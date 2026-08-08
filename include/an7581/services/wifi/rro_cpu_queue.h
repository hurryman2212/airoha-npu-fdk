/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_CPU_QUEUE_H
#define NPU_WIFI_RRO_CPU_QUEUE_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT UINT32_C(2048)
#define NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER UINT32_C(0xfe)
#define NPU_WIFI_RRO_CPU_QUEUE_PACKET_TYPE UINT32_C(4)
#define NPU_WIFI_RRO_CPU_QUEUE_PACKET_TYPE_MASK UINT32_C(0xff)
#define NPU_WIFI_RRO_CPU_QUEUE_SPECIAL_BIT UINT32_C(2)
#define NPU_WIFI_RRO_CPU_QUEUE_SPECIAL_OFFSET_MASK UINT32_C(0xfe)

struct npu_wifi_rro_cpu_queue_entry {
  uint32_t type;
  uint32_t buffer_id;
  uint32_t packet_control;
  uint32_t reserved;
};

enum npu_wifi_rro_cpu_queue_action {
  NPU_WIFI_RRO_CPU_QUEUE_DISPATCH_NORMAL = 0,
  NPU_WIFI_RRO_CPU_QUEUE_DISPATCH_SPECIAL,
  NPU_WIFI_RRO_CPU_QUEUE_RELEASE,
};

struct npu_wifi_rro_cpu_queue_pending {
  uint32_t type;
  uint32_t buffer_word;
  uint32_t packet_control;
  enum npu_wifi_rro_cpu_queue_action action;
  bool special;
};

struct npu_wifi_rro_cpu_queue_diagnostic_counters {
  volatile uint32_t *entries_enqueued;
  volatile uint32_t *full_waits;
  volatile uint32_t *entries_processed;
  volatile uint32_t *normal_entries;
};

struct npu_wifi_rro_cpu_queue {
  volatile struct npu_wifi_rro_cpu_queue_entry *entries;
  struct npu_wifi_rro_cpu_queue_pending pending;
  struct npu_wifi_rro_cpu_queue_diagnostic_counters diagnostic_counters;
  uint32_t dequeued_count;
  uint32_t normal_count;
  uint32_t special_count;
  uint32_t invalid_type_count;
  uint32_t rejected_count;
  uint16_t producer;
  uint16_t consumer;
  uint16_t entry_count;
  bool pending_valid;
};

typedef enum npu_runtime_result (*npu_wifi_rro_cpu_queue_dispatch)(
    void *context, int16_t buffer_id, uint16_t total_length, uint8_t flags,
    uint16_t fragment_length);
typedef enum npu_runtime_result (*npu_wifi_rro_cpu_queue_dispatch_special)(
    void *context, uint16_t buffer_id, uint16_t payload_length,
    uint16_t data_offset);
typedef enum npu_runtime_result (*npu_wifi_rro_cpu_queue_release)(
    void *context, uint16_t buffer_id);

struct npu_wifi_rro_cpu_queue_operations {
  npu_wifi_rro_cpu_queue_dispatch dispatch;
  npu_wifi_rro_cpu_queue_dispatch_special dispatch_special;
  npu_wifi_rro_cpu_queue_release release;
};

struct npu_wifi_rro_cpu_queue_result {
  uint32_t consumed_count;
  uint32_t normal_count;
  uint32_t special_count;
  uint32_t invalid_type_count;
  uint32_t rejected_count;
  uint16_t consumer;
  bool empty;
};

enum npu_runtime_result npu_wifi_rro_cpu_queue_initialize(
    struct npu_wifi_rro_cpu_queue *queue, volatile void *entry_memory,
    size_t entry_memory_size,
    const struct npu_wifi_rro_cpu_queue_diagnostic_counters
        *diagnostic_counters,
    uint16_t producer, uint16_t consumer);
enum npu_runtime_result npu_wifi_rro_cpu_queue_enqueue(void *context,
                                                       uint16_t buffer_id,
                                                       uint32_t packet_control);
enum npu_runtime_result npu_wifi_rro_cpu_queue_consume(
    struct npu_wifi_rro_cpu_queue *queue, uint32_t consume_budget,
    const struct npu_wifi_rro_cpu_queue_operations *operations, void *context,
    struct npu_wifi_rro_cpu_queue_result *result);

#endif

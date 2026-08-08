/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_PACKET_ID_POOL_H
#define NPU_WIFI_PACKET_ID_POOL_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"
#include "an7581/services/wifi/diagnostic_counters.h"

#define NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT UINT32_C(0x7000)
#define NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT UINT32_C(0x4000)
#define NPU_WIFI_PACKET_ID_POOL_RESET_LOCK_COUNT UINT32_C(2)

enum npu_wifi_packet_id_pool_lock {
  NPU_WIFI_PACKET_ID_RECYCLE_PRODUCER_LOCK = 0,
  NPU_WIFI_PACKET_ID_RECYCLE_CONSUMER_LOCK,
  NPU_WIFI_TOKEN_ID_CONSUMER_LOCK,
  NPU_WIFI_TOKEN_ID_PRODUCER_LOCK,
  NPU_WIFI_PACKET_ID_POOL_LOCK_COUNT,
};

typedef enum npu_runtime_result (*npu_wifi_packet_id_pool_lock_operation)(
    void *context, uint32_t lock_index);

struct npu_wifi_packet_id_pool_config {
  volatile uint16_t *token_entries;
  volatile uint16_t *recycle_entries;
  npu_wifi_packet_id_pool_lock_operation acquire;
  npu_wifi_packet_id_pool_lock_operation release;
  void *lock_context;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  uint32_t token_entry_capacity;
  uint32_t token_entry_count;
  uint32_t recycle_entry_count;
};

struct npu_wifi_packet_id_pool {
  volatile uint16_t *token_entries;
  volatile uint16_t *recycle_entries;
  npu_wifi_packet_id_pool_lock_operation acquire;
  npu_wifi_packet_id_pool_lock_operation release;
  void *lock_context;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  volatile uint16_t token_consumer;
  volatile uint16_t token_producer;
  volatile uint16_t recycle_consumer;
  volatile uint16_t recycle_producer;
  uint32_t token_entry_capacity;
  uint32_t token_entry_count;
  uint32_t token_allocation_count;
  uint32_t token_release_count;
  uint32_t token_force_reset_count;
  uint32_t packet_allocation_count;
  uint32_t packet_release_count;
  uint32_t reset_count;
  bool initialized;
};

enum npu_runtime_result npu_wifi_packet_id_pool_initialize(
    struct npu_wifi_packet_id_pool *pool,
    const struct npu_wifi_packet_id_pool_config *config);
enum npu_runtime_result
npu_wifi_packet_id_pool_allocate(struct npu_wifi_packet_id_pool *pool,
                                 uint16_t *packet_id);
enum npu_runtime_result
npu_wifi_packet_id_pool_release(struct npu_wifi_packet_id_pool *pool,
                                uint16_t packet_id);
enum npu_runtime_result
npu_wifi_token_id_pool_allocate(struct npu_wifi_packet_id_pool *pool,
                                uint16_t *token_id);
enum npu_runtime_result
npu_wifi_token_id_pool_release(struct npu_wifi_packet_id_pool *pool,
                               uint16_t token_id);
enum npu_runtime_result npu_wifi_packet_id_pool_set_token_entry_count(
    struct npu_wifi_packet_id_pool *pool, uint32_t token_entry_count);
enum npu_runtime_result npu_wifi_packet_id_pool_reset(void *context);

#endif

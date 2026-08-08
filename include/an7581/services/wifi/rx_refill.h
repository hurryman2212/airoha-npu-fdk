/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RX_REFILL_H
#define NPU_WIFI_RX_REFILL_H

#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_RX_REFILL_WORKER_MAX_RINGS 5U

struct npu_wifi_rx_refill_diagnostic_counters {
  volatile uint32_t *allocations;
  volatile uint32_t *allocation_failures;
};

struct npu_wifi_rx_refill_state {
  const struct npu_wifi_rx_ring_profile *profile;
  struct npu_wifi_rx_descriptor *descriptors;
  uint16_t *buffer_ids;
  struct npu_wifi_rx_refill_diagnostic_counters diagnostic_counters;
  uint32_t packet_buffer_base;
  uint32_t register_base;
  uint32_t descriptor_count;
  uint32_t producer_index;
  uint32_t last_refilled_index;
  uint32_t pending_publication_count;
  uint32_t publication_interval;
  uint8_t sequence;
  bool last_refilled_index_valid;
};

struct npu_wifi_rx_refill_result {
  uint32_t refilled_count;
  uint32_t publication_count;
  uint32_t producer_index;
  bool allocator_empty;
};

typedef bool (*npu_wifi_rx_refill_write32)(void *context, uint32_t address,
                                           uint32_t value);

enum npu_wifi_rx_refill_worker_event {
  NPU_WIFI_RX_REFILL_WORKER_HEARTBEAT = 0,
  NPU_WIFI_RX_REFILL_WORKER_EAGLE_CYCLE,
  NPU_WIFI_RX_REFILL_WORKER_MSDU_CYCLE,
};

struct npu_wifi_rx_refill_worker_profile {
  uint32_t ring_count;
  uint32_t msdu_first_ring;
  uint32_t refill_budgets[NPU_WIFI_RX_REFILL_WORKER_MAX_RINGS];
  uint8_t set_interfaces[NPU_WIFI_RX_REFILL_WORKER_MAX_RINGS];
  bool delay_on_msdu_allocator_empty;
};

struct npu_wifi_rx_refill_worker_ring {
  struct npu_wifi_rx_refill_state *state;
  uint32_t dma_index;
  const struct npu_wifi_rx_buffer_operations *buffer_operations;
  void *buffer_context;
  npu_wifi_rx_refill_write32 write32;
  void *write_context;
};

typedef void (*npu_wifi_rx_refill_worker_event_callback)(
    void *context, enum npu_wifi_rx_refill_worker_event event);
typedef void (*npu_wifi_rx_refill_worker_delay_callback)(void *context,
                                                         uint32_t iterations);

struct npu_wifi_rx_refill_worker_operations {
  npu_wifi_rx_refill_worker_event_callback event;
  npu_wifi_rx_refill_worker_delay_callback delay;
  void *context;
};

struct npu_wifi_rx_refill_worker_ring_result {
  enum npu_runtime_result status;
  struct npu_wifi_rx_refill_result refill;
  uint32_t attempt_count;
  uint32_t allocator_empty_count;
};

struct npu_wifi_rx_refill_worker_result {
  uint32_t ring_count;
  uint32_t completed_ring_count;
  struct npu_wifi_rx_refill_worker_ring_result
      rings[NPU_WIFI_RX_REFILL_WORKER_MAX_RINGS];
};

bool npu_wifi_rx_refill_publication_interval(uint32_t set_interface,
                                             uint32_t *publication_interval);
const struct npu_wifi_rx_ring_profile *
npu_wifi_rx_refill_find_profile(uint32_t set_interface);
enum npu_runtime_result npu_wifi_rx_refill_initialize(
    struct npu_wifi_rx_refill_state *state, uint32_t set_interface,
    void *descriptor_memory, size_t descriptor_memory_size,
    uint16_t *buffer_ids, uint32_t buffer_id_capacity,
    uint32_t descriptor_count,
    const struct npu_wifi_rx_refill_diagnostic_counters *diagnostic_counters,
    uint32_t packet_buffer_base, uint32_t register_base);
enum npu_runtime_result npu_wifi_rx_refill_process(
    struct npu_wifi_rx_refill_state *state, uint32_t dma_index,
    uint32_t refill_budget,
    const struct npu_wifi_rx_buffer_operations *buffer_operations,
    void *buffer_context, npu_wifi_rx_refill_write32 write32,
    void *write_context, struct npu_wifi_rx_refill_result *result);
enum npu_runtime_result npu_wifi_rx_refill_worker_cycle(
    const struct npu_wifi_rx_refill_worker_ring *rings, uint32_t ring_count,
    const struct npu_wifi_rx_refill_worker_operations *operations,
    struct npu_wifi_rx_refill_worker_result *result);

#endif

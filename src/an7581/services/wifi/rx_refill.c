/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rx_refill.h"

#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/rx_pcie.h"

#define NPU_WIFI_RX_BUFFER_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_RX_BUFFER_ADDRESS_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)

static const struct npu_wifi_rx_refill_worker_profile worker_profile = {
    .ring_count = NPU_WIFI_RX_REFILL_WORKER_MAX_RINGS,
    .msdu_first_ring = 2U,
    .refill_budgets =
        {
            NPU_WIFI_RX_DESCRIPTOR_LIMIT,
            NPU_WIFI_RX_MT7996_SECONDARY_DESCRIPTOR_LIMIT,
            NPU_WIFI_RX_MT7996_MSDU0_DESCRIPTOR_LIMIT,
            NPU_WIFI_RX_MT7996_MSDU1_DESCRIPTOR_LIMIT,
            NPU_WIFI_RX_MT7996_MSDU2_DESCRIPTOR_LIMIT,
        },
    .set_interfaces = {0U, 2U, 5U, 6U, 7U},
    .delay_on_msdu_allocator_empty = true,
};

static bool register_base_is_valid(uint32_t register_base) {
  return register_base != 0U &&
         register_base <=
             NPU_WIFI_MAX_HOST_ADDRESS - NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET &&
         (register_base & (sizeof(uint32_t) - 1U)) == 0U;
}

bool npu_wifi_rx_refill_publication_interval(uint32_t set_interface,
                                             uint32_t *publication_interval) {
  if (publication_interval == NULL)
    return false;

  if (set_interface == 0U || set_interface == 5U || set_interface == 6U ||
      set_interface == 7U)
    *publication_interval = 128U;
  else if (set_interface == 2U)
    *publication_interval = 64U;
  else
    return false;
  return true;
}

const struct npu_wifi_rx_ring_profile *
npu_wifi_rx_refill_find_profile(uint32_t set_interface) {
  return npu_wifi_rx_ring_find_profile(set_interface);
}

enum npu_runtime_result npu_wifi_rx_refill_initialize(
    struct npu_wifi_rx_refill_state *state, uint32_t set_interface,
    void *descriptor_memory, size_t descriptor_memory_size,
    uint16_t *buffer_ids, uint32_t buffer_id_capacity,
    uint32_t descriptor_count,
    const struct npu_wifi_rx_refill_diagnostic_counters *diagnostic_counters,
    uint32_t packet_buffer_base, uint32_t register_base) {
  const struct npu_wifi_rx_ring_profile *profile;
  uint32_t publication_interval;
  size_t required_size;

  profile = npu_wifi_rx_refill_find_profile(set_interface);
  if (state == NULL || profile == NULL || !profile->allocates_buffers ||
      !profile->stores_buffer_id ||
      (profile->kind != NPU_WIFI_RX_RING_EAGLE_DATA &&
       profile->kind != NPU_WIFI_RX_RING_MSDU_PAGE) ||
      !npu_wifi_rx_refill_publication_interval(set_interface,
                                               &publication_interval))
    return NPU_RUNTIME_INVALID_ARGUMENT;

  if (descriptor_memory == NULL || buffer_ids == NULL ||
      ((uintptr_t)descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      descriptor_count == 0U ||
      descriptor_count > profile->maximum_descriptor_count ||
      buffer_id_capacity < descriptor_count ||
      (diagnostic_counters != NULL &&
       ((diagnostic_counters->allocations != NULL &&
         ((uintptr_t)diagnostic_counters->allocations &
          (sizeof(uint32_t) - 1U)) != 0U) ||
        (diagnostic_counters->allocation_failures != NULL &&
         ((uintptr_t)diagnostic_counters->allocation_failures &
          (sizeof(uint32_t) - 1U)) != 0U))) ||
      !register_base_is_valid(register_base))
    return NPU_RUNTIME_OUT_OF_RANGE;

  required_size = (size_t)descriptor_count * profile->descriptor_size;
  if (descriptor_memory_size < required_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(state, 0U, sizeof(*state));
  state->profile = profile;
  state->descriptors = descriptor_memory;
  state->buffer_ids = buffer_ids;
  if (diagnostic_counters != NULL)
    state->diagnostic_counters = *diagnostic_counters;
  state->packet_buffer_base = packet_buffer_base;
  state->register_base = register_base;
  state->descriptor_count = descriptor_count;
  state->publication_interval = publication_interval;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
publish_cpu_index(struct npu_wifi_rx_refill_state *state,
                  npu_wifi_rx_refill_write32 write32, void *write_context,
                  struct npu_wifi_rx_refill_result *result) {
  if (state->pending_publication_count < state->publication_interval)
    return NPU_RUNTIME_SUCCESS;
  if (!state->last_refilled_index_valid ||
      !write32(write_context,
               state->register_base + NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET,
               state->last_refilled_index))
    return NPU_RUNTIME_IO_ERROR;

  state->pending_publication_count = 0U;
  ++result->publication_count;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
refill_one(struct npu_wifi_rx_refill_state *state,
           const struct npu_wifi_rx_buffer_operations *buffer_operations,
           void *buffer_context) {
  volatile struct npu_wifi_rx_descriptor *descriptor;
  uint32_t packet_offset;
  uint32_t packet_address;
  uint32_t producer_index = state->producer_index;
  uint16_t buffer_id;

  if (!buffer_operations->allocate(buffer_context, &buffer_id)) {
    if (state->diagnostic_counters.allocation_failures != NULL)
      ++*state->diagnostic_counters.allocation_failures;
    return NPU_RUNTIME_EMPTY;
  }
  if (state->diagnostic_counters.allocations != NULL)
    ++*state->diagnostic_counters.allocations;

  packet_offset = (uint32_t)buffer_id * state->profile->buffer_stride;
  if (state->packet_buffer_base > UINT32_MAX - packet_offset) {
    buffer_operations->release(buffer_context, buffer_id);
    return NPU_RUNTIME_OUT_OF_RANGE;
  }

  packet_address = ((state->packet_buffer_base + packet_offset) &
                    NPU_WIFI_RX_BUFFER_ADDRESS_MASK) |
                   NPU_WIFI_RX_BUFFER_ADDRESS_ALIAS;
  if (packet_address > UINT32_MAX - state->profile->packet_data_offset) {
    buffer_operations->release(buffer_context, buffer_id);
    return NPU_RUNTIME_OUT_OF_RANGE;
  }

  if (producer_index == 0U)
    state->sequence = (uint8_t)(((uint32_t)state->sequence + 1U) & 0xfU);

  descriptor = &state->descriptors[producer_index];
  descriptor->control = 0U;
  descriptor->buffer_address =
      packet_address + state->profile->packet_data_offset;
  descriptor->buffer_id = (uint32_t)buffer_id << 16U;
  descriptor->sequence_control = (uint32_t)state->sequence << 28U;
  descriptor->control = state->profile->initial_control;
  state->buffer_ids[producer_index] = buffer_id;

  state->last_refilled_index = producer_index;
  state->last_refilled_index_valid = true;
  ++producer_index;
  state->producer_index =
      producer_index == state->descriptor_count ? 0U : producer_index;
  ++state->pending_publication_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rx_refill_process(
    struct npu_wifi_rx_refill_state *state, uint32_t dma_index,
    uint32_t refill_budget,
    const struct npu_wifi_rx_buffer_operations *buffer_operations,
    void *buffer_context, npu_wifi_rx_refill_write32 write32,
    void *write_context, struct npu_wifi_rx_refill_result *result) {
  enum npu_runtime_result status;

  if (state == NULL || state->profile == NULL || state->descriptors == NULL ||
      state->buffer_ids == NULL || buffer_operations == NULL ||
      buffer_operations->allocate == NULL ||
      buffer_operations->release == NULL || write32 == NULL || result == NULL ||
      refill_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (dma_index >= state->descriptor_count ||
      state->producer_index >= state->descriptor_count ||
      state->publication_interval == 0U ||
      state->publication_interval > UINT8_MAX)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  status = publish_cpu_index(state, write32, write_context, result);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  while (state->producer_index != dma_index &&
         result->refilled_count < refill_budget) {
    status = refill_one(state, buffer_operations, buffer_context);
    if (status == NPU_RUNTIME_EMPTY) {
      result->allocator_empty = true;
      break;
    }
    if (status != NPU_RUNTIME_SUCCESS)
      return status;

    ++result->refilled_count;
    status = publish_cpu_index(state, write32, write_context, result);
    if (status != NPU_RUNTIME_SUCCESS) {
      result->producer_index = state->producer_index;
      return status;
    }
  }

  result->producer_index = state->producer_index;
  return result->allocator_empty ? NPU_RUNTIME_EMPTY : NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rx_refill_worker_cycle(
    const struct npu_wifi_rx_refill_worker_ring *rings, uint32_t ring_count,
    const struct npu_wifi_rx_refill_worker_operations *operations,
    struct npu_wifi_rx_refill_worker_result *result) {
  const struct npu_wifi_rx_refill_worker_profile *profile;
  enum npu_runtime_result overall_status = NPU_RUNTIME_SUCCESS;
  uint32_t ring_index;

  profile = &worker_profile;
  if (profile == NULL || rings == NULL || result == NULL ||
      ring_count != profile->ring_count)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  for (ring_index = 0U; ring_index < ring_count; ++ring_index) {
    const struct npu_wifi_rx_refill_worker_ring *ring = &rings[ring_index];

    if (ring->state == NULL || ring->state->profile == NULL ||
        ring->state->descriptors == NULL || ring->state->buffer_ids == NULL ||
        ring->state->profile->set_interface !=
            profile->set_interfaces[ring_index] ||
        ring->state->descriptor_count == 0U ||
        ring->state->descriptor_count >
            ring->state->profile->maximum_descriptor_count ||
        ring->state->producer_index >= ring->state->descriptor_count ||
        ring->state->publication_interval == 0U ||
        ring->state->publication_interval > UINT8_MAX ||
        ring->dma_index >= ring->state->descriptor_count ||
        ring->buffer_operations == NULL ||
        ring->buffer_operations->allocate == NULL ||
        ring->buffer_operations->release == NULL || ring->write32 == NULL)
      return NPU_RUNTIME_INVALID_ARGUMENT;
  }

  (void)npu_memset(result, 0U, sizeof(*result));
  result->ring_count = ring_count;
  if (operations != NULL && operations->event != NULL) {
    operations->event(operations->context, NPU_WIFI_RX_REFILL_WORKER_HEARTBEAT);
    operations->event(operations->context,
                      NPU_WIFI_RX_REFILL_WORKER_EAGLE_CYCLE);
  }

  for (ring_index = 0U; ring_index < ring_count; ++ring_index) {
    const struct npu_wifi_rx_refill_worker_ring *ring = &rings[ring_index];
    struct npu_wifi_rx_refill_worker_ring_result *ring_result =
        &result->rings[ring_index];
    uint32_t remaining_attempts = profile->refill_budgets[ring_index];

    if (ring_index == profile->msdu_first_ring) {
      if (operations != NULL && operations->delay != NULL)
        operations->delay(operations->context, 500U);
      if (operations != NULL && operations->event != NULL)
        operations->event(operations->context,
                          NPU_WIFI_RX_REFILL_WORKER_MSDU_CYCLE);
    }

    while (remaining_attempts != 0U) {
      struct npu_wifi_rx_refill_result pass_result;
      enum npu_runtime_result status;
      uint32_t pass_attempts;

      status = npu_wifi_rx_refill_process(
          ring->state, ring->dma_index, remaining_attempts,
          ring->buffer_operations, ring->buffer_context, ring->write32,
          ring->write_context, &pass_result);
      ring_result->status = status;
      ring_result->refill.refilled_count += pass_result.refilled_count;
      ring_result->refill.publication_count += pass_result.publication_count;
      ring_result->refill.producer_index = pass_result.producer_index;
      ring_result->refill.allocator_empty |= pass_result.allocator_empty;

      pass_attempts = pass_result.refilled_count;
      if (status == NPU_RUNTIME_EMPTY) {
        ++pass_attempts;
        ++ring_result->allocator_empty_count;
        overall_status = NPU_RUNTIME_EMPTY;
      }
      ring_result->attempt_count += pass_attempts;
      remaining_attempts -= pass_attempts;

      if (status == NPU_RUNTIME_SUCCESS)
        break;
      if (status != NPU_RUNTIME_EMPTY)
        return status;
      if (ring_index < profile->msdu_first_ring)
        break;
      if (profile->delay_on_msdu_allocator_empty && operations != NULL &&
          operations->delay != NULL) {
        /* MT7996 backs off after each failed MSDU allocation. */
        operations->delay(operations->context, 300U);
      }
      if (remaining_attempts == 0U)
        break;
    }
    ++result->completed_ring_count;
  }

  if (operations != NULL && operations->delay != NULL)
    operations->delay(operations->context, 500U);
  return overall_status;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_indication.h"

#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/rx_pcie.h"

#define NPU_WIFI_RRO_SEQUENCE_SHIFT UINT32_C(29)
#define NPU_WIFI_RRO_SEQUENCE_MASK UINT32_C(0x7)
#define NPU_WIFI_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)

_Static_assert(sizeof(struct npu_wifi_rro_indication_descriptor) ==
                   NPU_WIFI_RRO_INDICATION_DESCRIPTOR_SIZE,
               "Wi-Fi RRO indication descriptor layout changed");

static bool register_base_is_valid(uint32_t register_base) {
  return register_base != 0U &&
         register_base <=
             NPU_WIFI_MAX_HOST_ADDRESS - NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET &&
         (register_base & (sizeof(uint32_t) - 1U)) == 0U;
}

enum npu_runtime_result npu_wifi_rro_indication_initialize(
    struct npu_wifi_rro_indication_state *state, void *descriptor_memory,
    size_t descriptor_memory_size, uint32_t descriptor_count,
    uint32_t register_base, volatile uint32_t *available_counter) {
  size_t required_size;

  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (descriptor_memory == NULL ||
      ((uintptr_t)descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      descriptor_count != NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT ||
      !register_base_is_valid(register_base) ||
      (available_counter != NULL &&
       ((uintptr_t)available_counter & (sizeof(uint32_t) - 1U)) != 0U))
    return NPU_RUNTIME_OUT_OF_RANGE;

  required_size =
      (size_t)descriptor_count * NPU_WIFI_RRO_INDICATION_DESCRIPTOR_SIZE;
  if (descriptor_memory_size < required_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(state, 0U, sizeof(*state));
  state->descriptors = descriptor_memory;
  state->available_counter = available_counter;
  state->register_base = register_base;
  state->descriptor_count = descriptor_count;
  return NPU_RUNTIME_SUCCESS;
}

static void set_result_state(const struct npu_wifi_rro_indication_state *state,
                             struct npu_wifi_rro_indication_result *result) {
  result->consumer_index = state->consumer_index;
  result->expected_sequence = state->expected_sequence;
}

static enum npu_runtime_result
publish_cpu_index(struct npu_wifi_rro_indication_state *state,
                  npu_wifi_rro_indication_write32 write32, void *write_context,
                  struct npu_wifi_rro_indication_result *result) {
  if (state->pending_publication_count <
      NPU_WIFI_RRO_INDICATION_PUBLICATION_INTERVAL)
    return NPU_RUNTIME_SUCCESS;
  if (!state->last_consumed_index_valid ||
      !write32(write_context,
               state->register_base + NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET,
               state->last_consumed_index))
    return NPU_RUNTIME_IO_ERROR;

  state->pending_publication_count = 0U;
  ++result->publication_count;
  return NPU_RUNTIME_SUCCESS;
}

static bool state_is_valid(const struct npu_wifi_rro_indication_state *state) {
  return state->descriptors != NULL &&
         (state->available_counter == NULL ||
          ((uintptr_t)state->available_counter & (sizeof(uint32_t) - 1U)) ==
              0U) &&
         state->descriptor_count == NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT &&
         state->consumer_index < state->descriptor_count &&
         (uint32_t)state->expected_sequence <= NPU_WIFI_RRO_SEQUENCE_MASK &&
         state->pending_publication_count <=
             NPU_WIFI_RRO_INDICATION_PUBLICATION_INTERVAL &&
         (state->pending_publication_count == 0U ||
          state->last_consumed_index_valid) &&
         (!state->last_consumed_index_valid ||
          state->last_consumed_index < state->descriptor_count) &&
         register_base_is_valid(state->register_base);
}

enum npu_runtime_result npu_wifi_rro_indication_flush_cpu_index(
    struct npu_wifi_rro_indication_state *state,
    npu_wifi_rro_indication_write32 write32, void *write_context) {
  if (state == NULL || write32 == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!state_is_valid(state))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (state->pending_publication_count == 0U)
    return NPU_RUNTIME_SUCCESS;
  if (!state->last_consumed_index_valid)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (!write32(write_context,
               state->register_base + NPU_WIFI_RX_PCIE_CPU_INDEX_OFFSET,
               state->last_consumed_index))
    return NPU_RUNTIME_IO_ERROR;

  state->pending_publication_count = 0U;
  ++state->forced_publication_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_indication_process(
    struct npu_wifi_rro_indication_state *state, uint32_t consume_budget,
    npu_wifi_rro_indication_consume consume, void *consume_context,
    npu_wifi_rro_indication_write32 write32, void *write_context,
    struct npu_wifi_rro_indication_result *result) {
  enum npu_runtime_result status;

  if (state == NULL || consume == NULL || write32 == NULL || result == NULL ||
      consume_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!state_is_valid(state))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  set_result_state(state, result);
  status = publish_cpu_index(state, write32, write_context, result);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  while (result->consumed_count < consume_budget) {
    struct npu_wifi_rro_indication_descriptor descriptor;
    volatile struct npu_wifi_rro_indication_descriptor *source;
    uint32_t availability_word;
    uint32_t consumed_index = state->consumer_index;
    uint32_t observed_sequence;

    source = &state->descriptors[consumed_index];
    availability_word = source->count_control;
    observed_sequence = availability_word >> NPU_WIFI_RRO_SEQUENCE_SHIFT;
    if (observed_sequence != (uint32_t)state->expected_sequence) {
      result->unavailable = true;
      set_result_state(state, result);
      return result->consumed_count == 0U ? NPU_RUNTIME_EMPTY
                                          : NPU_RUNTIME_SUCCESS;
    }
    if (state->available_counter != NULL)
      ++*state->available_counter;

    descriptor.sequence_control = source->sequence_control;
    descriptor.count_control = source->count_control;
    status = consume(consume_context, &descriptor, consumed_index);
    if (status != NPU_RUNTIME_SUCCESS) {
      set_result_state(state, result);
      return status;
    }

    state->last_consumed_index = consumed_index;
    state->last_consumed_index_valid = true;
    ++consumed_index;
    if (consumed_index == state->descriptor_count) {
      consumed_index = 0U;
      state->expected_sequence =
          (uint8_t)((observed_sequence + 1U) & NPU_WIFI_RRO_SEQUENCE_MASK);
    }
    state->consumer_index = consumed_index;
    ++state->pending_publication_count;
    ++result->consumed_count;

    status = publish_cpu_index(state, write32, write_context, result);
    set_result_state(state, result);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
  }

  return NPU_RUNTIME_SUCCESS;
}

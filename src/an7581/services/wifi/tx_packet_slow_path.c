/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tx_packet_slow_path.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct npu_wifi_tx_packet_descriptor) ==
                   NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE,
               "Wi-Fi TX slow-path input descriptor layout changed");
_Static_assert(sizeof(struct npu_wifi_tx_descriptor) ==
                   NPU_WIFI_TX_DESCRIPTOR_SIZE,
               "Wi-Fi TX slow-path output descriptor layout changed");
_Static_assert(sizeof(struct npu_wifi_tx_ring_registers) == 16U,
               "Wi-Fi TX slow-path register layout changed");
_Static_assert(offsetof(struct npu_wifi_tx_buffer_space_record,
                        token_control) == 8U * sizeof(uint32_t),
               "Wi-Fi TX slow-path token-control offset changed");
_Static_assert(NPU_WIFI_TX_SLOW_PATH_RECORD_COPY_SIZE >=
                   offsetof(struct npu_wifi_tx_buffer_space_record,
                            packet_length) +
                       sizeof(uint32_t),
               "Wi-Fi TX slow-path copy omits populated record fields");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool output_count_is_supported(uint32_t output_count,
                                      uint32_t band_index) {
  uint32_t mt7996_count = band_index == 0U
                              ? NPU_WIFI_MT7996_TX_BAND0_DESCRIPTOR_COUNT
                              : NPU_WIFI_MT7996_TX_SECONDARY_DESCRIPTOR_COUNT;

  return output_count == mt7996_count;
}

static bool band_configuration_is_valid(
    const struct npu_wifi_tx_slow_path_band_config *band, uint32_t band_index) {
  uint32_t staging_offset;
  uint32_t staging_span;

  if (band->input_descriptors == NULL || band->output_descriptors == NULL ||
      band->registers == NULL || band->staging_memory == NULL ||
      !output_count_is_supported(band->output_descriptor_count, band_index) ||
      band->staging_physical_base == 0U ||
      band->staging_physical_base > NPU_WIFI_TX_PACKET_MAX_HOST_ADDRESS ||
      !pointer_is_aligned(band->input_descriptors, sizeof(uint32_t)) ||
      !pointer_is_aligned(band->output_descriptors, sizeof(uint32_t)) ||
      !pointer_is_aligned(band->registers, sizeof(uint32_t)) ||
      !pointer_is_aligned(band->staging_memory, sizeof(uint32_t)) ||
      (band->diagnostic_counters.waits_or_publish_failures != NULL &&
       !pointer_is_aligned(band->diagnostic_counters.waits_or_publish_failures,
                           sizeof(uint32_t))) ||
      (band->diagnostic_counters.descriptor_publish_retries != NULL &&
       !pointer_is_aligned(band->diagnostic_counters.descriptor_publish_retries,
                           sizeof(uint32_t))) ||
      (band->diagnostic_counters.lookahead_descriptor_waits != NULL &&
       !pointer_is_aligned(band->diagnostic_counters.lookahead_descriptor_waits,
                           sizeof(uint32_t))) ||
      (band->staging_physical_base &
       (NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE - 1U)) != 0U)
    return false;

  staging_span =
      band->output_descriptor_count * NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE;
  staging_offset =
      band->staging_physical_base & NPU_WIFI_TX_SLOW_PATH_ADDRESS_MASK;
  return band->staging_memory_size >= staging_span &&
         staging_span <=
             (NPU_WIFI_TX_SLOW_PATH_ADDRESS_MASK + 1U) - staging_offset;
}

enum npu_runtime_result npu_wifi_tx_slow_path_initialize(
    struct npu_wifi_tx_slow_path *slow_path,
    const struct npu_wifi_tx_slow_path_config *config) {
  uint32_t band_index;

  if (slow_path == NULL || config == NULL || config->copy == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (slow_path->initialized)
    return NPU_RUNTIME_REJECTED;
  if (config->token_id_limit == 0U ||
      config->token_id_limit > NPU_WIFI_TX_SLOW_PATH_TOKEN_MASK + 1U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  for (band_index = 0U; band_index < NPU_WIFI_MT7996_TX_BAND_COUNT;
       ++band_index) {
    if (!band_configuration_is_valid(&config->band[band_index], band_index))
      return NPU_RUNTIME_OUT_OF_RANGE;
  }
  if (config->producer_state != NULL &&
      !npu_wifi_tx_producer_state_is_valid_for_counts(
          config->producer_state, config->band[0].output_descriptor_count,
          config->band[1].output_descriptor_count))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(slow_path, 0U, sizeof(*slow_path));
  for (band_index = 0U; band_index < NPU_WIFI_MT7996_TX_BAND_COUNT;
       ++band_index)
    slow_path->band[band_index] = config->band[band_index];
  slow_path->copy = config->copy;
  slow_path->delay = config->delay;
  slow_path->copy_context = config->copy_context;
  slow_path->delay_context = config->delay_context;
  slow_path->producer_state = config->producer_state != NULL
                                  ? config->producer_state
                                  : &slow_path->local_producer_state;
  slow_path->stop_requested = config->stop_requested;
  slow_path->token_id_limit = config->token_id_limit;
  slow_path->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint16_t advance_index(uint16_t index, uint32_t count) {
  ++index;
  if ((uint32_t)index == count)
    index = 0U;
  return index;
}

static uint32_t device_alias(uint32_t address) {
  return (address & NPU_WIFI_TX_SLOW_PATH_ADDRESS_MASK) |
         NPU_WIFI_TX_SLOW_PATH_DEVICE_ALIAS;
}

static void consume_input_descriptor(
    volatile struct npu_wifi_tx_packet_descriptor *descriptor) {
  descriptor->packet_address = 0U;
  descriptor->token_control = NPU_WIFI_TX_SLOW_PATH_INVALID_TOKEN_CONTROL;
  descriptor->status &= ~NPU_WIFI_TX_SLOW_PATH_READY_MASK;
  an7581_dma_memory_barrier();
}

static bool input_descriptor_is_ready(
    const volatile struct npu_wifi_tx_packet_descriptor *descriptor) {
  return (descriptor->status & NPU_WIFI_TX_SLOW_PATH_READY_MASK) ==
         NPU_WIFI_TX_SLOW_PATH_READY;
}

static bool output_descriptor_is_available(
    const volatile struct npu_wifi_tx_descriptor *descriptor) {
  an7581_dma_memory_barrier();
  return (descriptor->control & NPU_WIFI_TX_DESCRIPTOR_READY) != 0U;
}

static bool
slow_path_stop_requested(const struct npu_wifi_tx_slow_path *slow_path) {
  return slow_path->stop_requested != NULL && *slow_path->stop_requested;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

static bool wait_for_current_output(
    struct npu_wifi_tx_slow_path *slow_path,
    const struct npu_wifi_tx_slow_path_band_config *band,
    const volatile struct npu_wifi_tx_descriptor *descriptor) {
  uint32_t attempts_remaining = NPU_WIFI_TX_SLOW_PATH_CURRENT_WAIT_LIMIT;

  while (!output_descriptor_is_available(descriptor) &&
         attempts_remaining != 0U && !slow_path_stop_requested(slow_path)) {
    if (slow_path->delay == NULL)
      return false;
    slow_path->delay(slow_path->delay_context,
                     NPU_WIFI_TX_SLOW_PATH_RETRY_DELAY);
    --attempts_remaining;
    increment_counter(band->diagnostic_counters.waits_or_publish_failures);
  }
  return output_descriptor_is_available(descriptor);
}

static bool wait_for_lookahead_output(
    struct npu_wifi_tx_slow_path *slow_path,
    const struct npu_wifi_tx_slow_path_band_config *band,
    const volatile struct npu_wifi_tx_descriptor *descriptor) {
  while (!output_descriptor_is_available(descriptor) &&
         !slow_path_stop_requested(slow_path)) {
    if (slow_path->delay == NULL)
      return false;
    increment_counter(band->diagnostic_counters.lookahead_descriptor_waits);
    slow_path->delay(slow_path->delay_context,
                     NPU_WIFI_TX_SLOW_PATH_RETRY_DELAY);
  }
  return output_descriptor_is_available(descriptor);
}

static bool input_descriptor_is_valid(
    const volatile struct npu_wifi_tx_packet_descriptor *descriptor,
    uint32_t token_id_limit, uint16_t *token_id) {
  uint32_t token = descriptor->token_control & NPU_WIFI_TX_SLOW_PATH_TOKEN_MASK;
  uint32_t validity =
      descriptor->token_control >> NPU_WIFI_TX_SLOW_PATH_TOKEN_VALIDITY_SHIFT;

  if (token >= token_id_limit || validity == 0U ||
      descriptor->buffer_address == 0U ||
      (descriptor->buffer_address & (sizeof(uint32_t) - 1U)) != 0U)
    return false;
  *token_id = (uint16_t)token;
  return true;
}

static void patch_record_token(volatile uint8_t *staging_memory,
                               uint32_t staging_offset, uint16_t token_id) {
  volatile struct npu_wifi_tx_buffer_space_record *record =
      __builtin_assume_aligned(staging_memory + staging_offset,
                               sizeof(uint32_t));
  uint32_t token_control = record->token_control;

  if (token_control >> 16U != (uint32_t)token_id)
    record->token_control = (token_control & NPU_WIFI_TX_SLOW_PATH_TOKEN_MASK) |
                            ((uint32_t)token_id << 16U);
}

static bool
publish_output_descriptor(struct npu_wifi_tx_slow_path *slow_path,
                          const struct npu_wifi_tx_slow_path_band_config *band,
                          volatile struct npu_wifi_tx_descriptor *output,
                          uint32_t destination_address, uint32_t metadata) {
  uint32_t retries_remaining = NPU_WIFI_TX_SLOW_PATH_PUBLISH_RETRY_LIMIT;

  for (;;) {
    output->buffer0 = destination_address;
    output->buffer1 = metadata;
    output->information = 0U;
    an7581_dma_memory_barrier();
    output->control = NPU_WIFI_TX_SLOW_PATH_OUTPUT_CONTROL;
    an7581_dma_memory_barrier();
    if (!output_descriptor_is_available(output))
      return true;
    if (retries_remaining == 0U)
      return false;
    if (slow_path->delay != NULL)
      slow_path->delay(slow_path->delay_context,
                       NPU_WIFI_TX_SLOW_PATH_RETRY_DELAY);
    --retries_remaining;
    increment_counter(band->diagnostic_counters.descriptor_publish_retries);
  }
}

enum npu_runtime_result
npu_wifi_tx_slow_path_process(struct npu_wifi_tx_slow_path *slow_path,
                              uint32_t band_index,
                              struct npu_wifi_tx_slow_path_result *result) {
  struct npu_wifi_tx_slow_path_band_config *band;
  volatile struct npu_wifi_tx_packet_descriptor *input;
  volatile struct npu_wifi_tx_descriptor *next_output;
  volatile struct npu_wifi_tx_descriptor *output;
  enum npu_runtime_result status;
  uint32_t destination_address;
  uint32_t staging_offset;
  uint16_t next_output_index;
  uint16_t input_index;
  uint16_t output_index;
  uint16_t token_id = 0U;

  if (slow_path == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  result->band = band_index;
  if (!slow_path->initialized || band_index >= NPU_WIFI_MT7996_TX_BAND_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  band = &slow_path->band[band_index];
  input_index = slow_path->input_consumer[band_index];
  output_index = slow_path->producer_state->index[band_index];
  result->input_index = input_index;
  result->output_index = output_index;
  input = &band->input_descriptors[input_index];
  an7581_dma_memory_barrier();
  if (!input_descriptor_is_ready(input))
    return NPU_RUNTIME_EMPTY;
  if (!input_descriptor_is_valid(input, slow_path->token_id_limit, &token_id)) {
    consume_input_descriptor(input);
    slow_path->input_consumer[band_index] =
        advance_index(input_index, NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT);
    ++slow_path->malformed_packet_count;
    result->consumed = true;
    result->malformed = true;
    return NPU_RUNTIME_REJECTED;
  }
  result->token_id = token_id;

  next_output_index =
      advance_index(output_index, band->output_descriptor_count);
  output = &band->output_descriptors[output_index];
  next_output = &band->output_descriptors[next_output_index];
  an7581_dma_memory_barrier();
  if (!wait_for_current_output(slow_path, band, output) ||
      !wait_for_lookahead_output(slow_path, band, next_output)) {
    ++slow_path->full_count;
    return NPU_RUNTIME_FULL;
  }

  staging_offset =
      (uint32_t)output_index * NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE;
  destination_address =
      device_alias(band->staging_physical_base + staging_offset);
  status = slow_path->copy(
      slow_path->copy_context, device_alias(input->buffer_address),
      destination_address, NPU_WIFI_TX_SLOW_PATH_RECORD_COPY_SIZE);
  if (status != NPU_RUNTIME_SUCCESS) {
    ++slow_path->copy_failure_count;
    return status;
  }

  an7581_dma_memory_barrier();
  patch_record_token(band->staging_memory, staging_offset, token_id);
  if (!publish_output_descriptor(slow_path, band, output, destination_address,
                                 input->packet_address)) {
    increment_counter(band->diagnostic_counters.waits_or_publish_failures);
    consume_input_descriptor(input);
    slow_path->input_consumer[band_index] =
        advance_index(input_index, NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT);
    ++slow_path->full_count;
    result->consumed = true;
    return NPU_RUNTIME_FULL;
  }
  slow_path->producer_state->index[band_index] = next_output_index;
  band->registers->cpu_index = next_output_index;

  consume_input_descriptor(input);
  slow_path->input_consumer[band_index] =
      advance_index(input_index, NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT);
  ++slow_path->forwarded_packet_count;
  result->consumed = true;
  result->forwarded = true;
  return NPU_RUNTIME_SUCCESS;
}

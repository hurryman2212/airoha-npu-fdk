/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tr471/runtime.h"

#include "an7581/runtime/endian.h"
#include "an7581/runtime/memory.h"

#define NPU_TR471_MARKER_HIGH UINT8_C(0xbe)
#define NPU_TR471_MARKER_LOW UINT8_C(0xef)
#define NPU_TR471_SEQUENCE_OFFSET 4U
#define NPU_TR471_PAYLOAD_SIZE_OFFSET 8U
#define NPU_TR471_RESERVED_OFFSET 10U
#define NPU_TR471_CONFIGURED_SECONDS_OFFSET 12U
#define NPU_TR471_CONFIGURED_NANOSECONDS_OFFSET 16U
#define NPU_TR471_TRANSMITTED_SECONDS_OFFSET 20U
#define NPU_TR471_TRANSMITTED_NANOSECONDS_OFFSET 24U
#define NPU_TR471_TRAILER_OFFSET 28U
#define NPU_TR471_IPV4_TOTAL_LENGTH_OFFSET 16U
#define NPU_TR471_IPV4_UDP_LENGTH_OFFSET 38U
#define NPU_TR471_IPV6_PAYLOAD_LENGTH_OFFSET 18U
#define NPU_TR471_IPV6_UDP_LENGTH_OFFSET 58U
#define NPU_TR471_IPV4_LENGTH_OVERHEAD 28U
#define NPU_TR471_UDP_LENGTH_OVERHEAD 8U
#define NPU_TR471_SCHEDULE_PERIOD 10U
#define NPU_TR471_NANOSECONDS_PER_SECOND UINT32_C(1000000000)
#define NPU_TR471_NANOSECONDS_PER_MILLISECOND UINT32_C(1000000)
#define NPU_TR471_MILLISECONDS_PER_SECOND UINT32_C(1000)
#define NPU_TR471_ROUND_TO_MILLISECOND UINT32_C(500000)

static uint32_t load_big_endian_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void store_big_endian_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static bool selected_flow_is_valid(const struct npu_tr471_state *state) {
  if (state->selected_ip_version == NPU_TR471_IPV4)
    return state->ipv4.valid;

  return state->ipv6.valid;
}

static size_t test_header_offset(const struct npu_tr471_state *state) {
  if (state->selected_ip_version == NPU_TR471_IPV4)
    return NPU_TR471_IPV4_TEST_HEADER_OFFSET;

  return NPU_TR471_IPV6_TEST_HEADER_OFFSET;
}

static uint32_t elapsed_milliseconds(const struct npu_tr471_clock *current,
                                     const struct npu_tr471_clock *start) {
  uint32_t seconds;
  int32_t nanoseconds;
  int32_t milliseconds;

  seconds = current->seconds - start->seconds;
  nanoseconds = (int32_t)(current->nanoseconds +
                          NPU_TR471_ROUND_TO_MILLISECOND - start->nanoseconds);
  milliseconds = (int32_t)(seconds * NPU_TR471_MILLISECONDS_PER_SECOND) +
                 nanoseconds / (int32_t)NPU_TR471_NANOSECONDS_PER_MILLISECOND;
  if (milliseconds < 0)
    return 0U;

  return (uint32_t)milliseconds;
}

static struct npu_tr471_clock load_clock(const uint8_t *header,
                                         size_t seconds_offset,
                                         size_t nanoseconds_offset) {
  struct npu_tr471_clock clock;

  clock.seconds = load_big_endian_u32(header + seconds_offset);
  clock.nanoseconds = load_big_endian_u32(header + nanoseconds_offset);
  return clock;
}

static bool clocks_are_equal(const struct npu_tr471_clock *left,
                             const struct npu_tr471_clock *right) {
  return left->seconds == right->seconds &&
         left->nanoseconds == right->nanoseconds;
}

enum npu_runtime_result npu_tr471_timer_tick(struct npu_tr471_state *state) {
  if (state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  ++state->periodic_counter;
  if (state->periodic_counter % NPU_TR471_TIMER_TICKS_PER_TEN_MILLISECONDS ==
      0U)
    ++state->ten_millisecond_counter;

  state->clock.nanoseconds += NPU_TR471_TIMER_TICK_NANOSECONDS;
  if (state->clock.nanoseconds >= NPU_TR471_NANOSECONDS_PER_SECOND) {
    state->clock.nanoseconds -= NPU_TR471_NANOSECONDS_PER_SECOND;
    ++state->clock.seconds;
  }
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_transmit_schedule_step(struct npu_tr471_state *state,
                                 uint32_t periodic_counter,
                                 struct npu_tr471_transmit_batch *batches,
                                 size_t batch_capacity, size_t *batch_count) {
  struct npu_tr471_transmit_batch planned[NPU_TR471_TRANSMIT_BATCH_LIMIT];
  size_t required_count = 1U;

  if (state == NULL || batch_count == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  *batch_count = 0U;
  if (!state->running || state->transmit_enabled == 0U ||
      !state->transmit.valid)
    return NPU_RUNTIME_REJECTED;
  if (periodic_counter == state->last_schedule_counter)
    return NPU_RUNTIME_SUCCESS;

  if (periodic_counter / NPU_TR471_SCHEDULE_PERIOD !=
      state->last_schedule_counter / NPU_TR471_SCHEDULE_PERIOD)
    required_count = 2U;
  if (batches == NULL || batch_capacity < required_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  planned[0].packet_count = state->transmit.primary_packet_count;
  planned[0].payload_size = state->transmit.primary_payload_size;
  planned[0].final_payload_size = 0U;
  if (required_count == 2U) {
    planned[1].packet_count = state->transmit.burst_packet_count;
    planned[1].payload_size = state->transmit.burst_payload_size;
    planned[1].final_payload_size = state->transmit.final_payload_size;
  }

  (void)npu_memcpy(batches, planned, required_count * sizeof(planned[0]));
  state->last_schedule_counter = periodic_counter;
  *batch_count = required_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_packet_build(struct npu_tr471_state *state, uint32_t payload_size,
                       const struct npu_tr471_clock *transmitted_at,
                       uint8_t *output, size_t output_extent,
                       size_t *packet_size) {
  struct npu_tr471_clock configured_at;
  uint32_t next_sequence;
  uint32_t udp_length;
  size_t header_offset;
  size_t output_size;
  size_t template_size;
  uint8_t *header;

  if (state == NULL || transmitted_at == NULL || output == NULL ||
      packet_size == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!state->transmit.valid || !selected_flow_is_valid(state) ||
      payload_size < NPU_TR471_TEST_HEADER_SIZE ||
      payload_size > UINT16_MAX - NPU_TR471_IPV4_LENGTH_OVERHEAD)
    return NPU_RUNTIME_OUT_OF_RANGE;

  header_offset = test_header_offset(state);
  output_size = header_offset + payload_size;
  if (output_extent < output_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  next_sequence = state->packet_sequence + 1U;
  configured_at = state->transmit.configured_at;
  if (next_sequence == 1U) {
    configured_at.seconds = 0U;
    configured_at.nanoseconds = 0U;
  }

  (void)npu_memset(output, 0U, output_size);
  if (!npu_tr471_packet_template_build(state, output, output_extent,
                                       &template_size))
    return NPU_RUNTIME_OUT_OF_RANGE;
  (void)template_size;

  udp_length = payload_size + NPU_TR471_UDP_LENGTH_OVERHEAD;
  if (state->selected_ip_version == NPU_TR471_IPV4) {
    npu_store_big_endian_u16(
        output + NPU_TR471_IPV4_TOTAL_LENGTH_OFFSET,
        (uint16_t)(payload_size + NPU_TR471_IPV4_LENGTH_OVERHEAD));
    npu_store_big_endian_u16(output + NPU_TR471_IPV4_UDP_LENGTH_OFFSET,
                             (uint16_t)udp_length);
  } else {
    npu_store_big_endian_u16(output + NPU_TR471_IPV6_PAYLOAD_LENGTH_OFFSET,
                             (uint16_t)udp_length);
    npu_store_big_endian_u16(output + NPU_TR471_IPV6_UDP_LENGTH_OFFSET,
                             (uint16_t)udp_length);
  }

  header = output + header_offset;
  header[0] = NPU_TR471_MARKER_HIGH;
  header[1] = NPU_TR471_MARKER_LOW;
  npu_store_big_endian_u16(header + 2U, 0U);
  store_big_endian_u32(header + NPU_TR471_SEQUENCE_OFFSET, next_sequence);
  npu_store_big_endian_u16(header + NPU_TR471_PAYLOAD_SIZE_OFFSET,
                           (uint16_t)payload_size);
  npu_store_big_endian_u16(header + NPU_TR471_RESERVED_OFFSET, 0U);
  store_big_endian_u32(header + NPU_TR471_CONFIGURED_SECONDS_OFFSET,
                       configured_at.seconds);
  store_big_endian_u32(header + NPU_TR471_CONFIGURED_NANOSECONDS_OFFSET,
                       configured_at.nanoseconds);
  store_big_endian_u32(header + NPU_TR471_TRANSMITTED_SECONDS_OFFSET,
                       transmitted_at->seconds);
  store_big_endian_u32(header + NPU_TR471_TRANSMITTED_NANOSECONDS_OFFSET,
                       transmitted_at->nanoseconds);
  store_big_endian_u32(header + NPU_TR471_TRAILER_OFFSET, 0U);

  state->packet_sequence = next_sequence;
  *packet_size = output_size;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_tr471_receive_packet(struct npu_tr471_state *state,
                                                 const uint8_t *packet,
                                                 size_t packet_size) {
  struct npu_tr471_result_counters *counters;
  struct npu_tr471_clock configured_at;
  struct npu_tr471_clock transmitted_at;
  const uint8_t *header;
  uint32_t baseline_ms;
  uint32_t expected_sequence;
  uint32_t payload_size;
  uint32_t sequence;
  size_t header_offset;

  if (state == NULL || packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  header_offset = test_header_offset(state);
  if (packet_size < header_offset + NPU_TR471_TEST_HEADER_SIZE)
    return NPU_RUNTIME_OUT_OF_RANGE;
  header = packet + header_offset;
  if (header[0] != NPU_TR471_MARKER_HIGH || header[1] != NPU_TR471_MARKER_LOW ||
      npu_load_big_endian_u16(header + 2U) != 0U)
    return NPU_RUNTIME_REJECTED;

  payload_size =
      (uint32_t)npu_load_big_endian_u16(header + NPU_TR471_PAYLOAD_SIZE_OFFSET);
  if (payload_size < NPU_TR471_TEST_HEADER_SIZE ||
      payload_size > packet_size - header_offset)
    return NPU_RUNTIME_OUT_OF_RANGE;

  if (state->result.reset_pending) {
    (void)npu_memset(&state->result.counters, 0U,
                     sizeof(state->result.counters));
    state->result.reset_pending = false;
  }
  counters = &state->result.counters;
  ++counters->received_packet_count;
  counters->received_payload_bytes += payload_size;

  sequence = load_big_endian_u32(header + NPU_TR471_SEQUENCE_OFFSET);
  expected_sequence = state->last_received_sequence + 1U;
  if (expected_sequence > sequence) {
    ++counters->out_of_order_packet_count;
    state->result.valid = true;
    return NPU_RUNTIME_SUCCESS;
  }
  if (expected_sequence < sequence)
    counters->missing_packet_count += sequence - expected_sequence;
  state->last_received_sequence = sequence;

  configured_at = load_clock(header, NPU_TR471_CONFIGURED_SECONDS_OFFSET,
                             NPU_TR471_CONFIGURED_NANOSECONDS_OFFSET);
  if (!clocks_are_equal(&configured_at, &state->reference_clock)) {
    baseline_ms = elapsed_milliseconds(&state->clock, &configured_at);
    if (baseline_ms < counters->minimum_start_delay_ms) {
      counters->minimum_start_delay_ms = baseline_ms;
      counters->latency_valid = 1U;
    }
    counters->start_delay_variation_ms =
        baseline_ms - counters->minimum_start_delay_ms;
    state->reference_clock = configured_at;
  }

  transmitted_at = load_clock(header, NPU_TR471_TRANSMITTED_SECONDS_OFFSET,
                              NPU_TR471_TRANSMITTED_NANOSECONDS_OFFSET);
  counters->latest_latency_ms =
      elapsed_milliseconds(&state->clock, &transmitted_at);
  counters->latency_valid = 1U;
  state->result.valid = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t
pending_packet_count(const struct npu_tr471_runtime_io *runtime) {
  uint32_t count = 0U;
  size_t index;

  for (index = runtime->pending_batch_index;
       index < runtime->pending_batch_count; ++index)
    count += runtime->pending[index].remaining_packet_count;
  return count;
}

static void load_pending_batches(struct npu_tr471_runtime_io *runtime,
                                 const struct npu_tr471_transmit_batch *batches,
                                 size_t batch_count) {
  size_t index;

  for (index = 0U; index < batch_count; ++index) {
    runtime->pending[index].remaining_packet_count =
        batches[index].packet_count +
        (uint32_t)(batches[index].final_payload_size != 0U);
    runtime->pending[index].payload_size = batches[index].payload_size;
    runtime->pending[index].final_payload_size =
        batches[index].final_payload_size;
  }
  runtime->pending_batch_count = batch_count;
  runtime->pending_batch_index = 0U;
}

static void clear_pending_batches(struct npu_tr471_runtime_io *runtime) {
  (void)npu_memset(runtime->pending, 0U, sizeof(runtime->pending));
  runtime->pending_batch_count = 0U;
  runtime->pending_batch_index = 0U;
}

static void skip_empty_pending_batches(struct npu_tr471_runtime_io *runtime) {
  while (
      runtime->pending_batch_index < runtime->pending_batch_count &&
      runtime->pending[runtime->pending_batch_index].remaining_packet_count ==
          0U)
    ++runtime->pending_batch_index;

  if (runtime->pending_batch_index == runtime->pending_batch_count) {
    runtime->pending_batch_index = 0U;
    runtime->pending_batch_count = 0U;
  }
}

static enum npu_runtime_result
plan_transmit_work(struct npu_tr471_runtime_io *runtime,
                   uint32_t periodic_counter) {
  struct npu_tr471_transmit_batch batches[NPU_TR471_TRANSMIT_BATCH_LIMIT];
  size_t batch_count;
  enum npu_runtime_result status;

  if (!runtime->state->running || runtime->state->transmit_enabled == 0U ||
      !runtime->state->transmit.valid) {
    clear_pending_batches(runtime);
    return NPU_RUNTIME_REJECTED;
  }

  skip_empty_pending_batches(runtime);
  if (runtime->pending_batch_count != 0U)
    return NPU_RUNTIME_SUCCESS;

  status = npu_tr471_transmit_schedule_step(
      runtime->state, periodic_counter, batches, NPU_TR471_TRANSMIT_BATCH_LIMIT,
      &batch_count);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  load_pending_batches(runtime, batches, batch_count);
  skip_empty_pending_batches(runtime);
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t
pending_payload_size(const struct npu_tr471_pending_transmit_batch *batch) {
  if (batch->remaining_packet_count == 1U && batch->final_payload_size != 0U)
    return batch->final_payload_size;

  return batch->payload_size;
}

static enum npu_runtime_result
transmit_one_packet(struct npu_tr471_runtime_io *runtime) {
  struct npu_tr471_pending_transmit_batch *batch;
  struct npu_tr471_tdma_tx_slot slot;
  uint32_t previous_sequence;
  uint32_t payload_size;
  size_t packet_size;
  enum npu_runtime_result status;

  skip_empty_pending_batches(runtime);
  if (runtime->pending_batch_count == 0U)
    return NPU_RUNTIME_EMPTY;

  status = npu_tr471_tdma_tx_take(runtime->tdma, &slot);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  batch = &runtime->pending[runtime->pending_batch_index];
  payload_size = pending_payload_size(batch);
  previous_sequence = runtime->state->packet_sequence;
  status = npu_tr471_packet_build(runtime->state, payload_size,
                                  &runtime->state->clock, slot.packet,
                                  slot.capacity, &packet_size);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  status = npu_tr471_tdma_tx_submit(runtime->tdma, &slot, (uint32_t)packet_size,
                                    NPU_TR471_TDMA_TX_MESSAGE1);
  if (status != NPU_RUNTIME_SUCCESS) {
    runtime->state->packet_sequence = previous_sequence;
    return status;
  }

  --batch->remaining_packet_count;
  ++runtime->transmitted_packet_count;
  skip_empty_pending_batches(runtime);
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
receive_one_packet(struct npu_tr471_runtime_io *runtime) {
  struct npu_tr471_tdma_rx_packet packet;
  enum npu_runtime_result release_status;
  enum npu_runtime_result status;

  status = npu_tr471_tdma_rx_take(runtime->tdma, &packet);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  status =
      npu_tr471_receive_packet(runtime->state, packet.packet, packet.length);
  release_status = npu_tr471_tdma_rx_release(runtime->tdma, &packet);
  if (release_status != NPU_RUNTIME_SUCCESS)
    return release_status;

  ++runtime->received_packet_count;
  if (status != NPU_RUNTIME_SUCCESS)
    ++runtime->rejected_receive_count;
  return status;
}

enum npu_runtime_result
npu_tr471_runtime_io_initialize(struct npu_tr471_runtime_io *runtime,
                                struct npu_tr471_state *state,
                                struct npu_tr471_tdma *tdma) {
  if (runtime == NULL || state == NULL || tdma == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (runtime->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!tdma->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(runtime, 0U, sizeof(*runtime));
  runtime->state = state;
  runtime->tdma = tdma;
  runtime->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_runtime_io_cancel_pending(struct npu_tr471_runtime_io *runtime) {
  if (runtime == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!runtime->initialized)
    return NPU_RUNTIME_REJECTED;

  clear_pending_batches(runtime);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_runtime_io_step(struct npu_tr471_runtime_io *runtime,
                          uint32_t periodic_counter, uint32_t transmit_budget,
                          uint32_t receive_budget,
                          struct npu_tr471_runtime_step_result *result) {
  enum npu_runtime_result status;

  if (runtime == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!runtime->initialized)
    return NPU_RUNTIME_REJECTED;
  if (transmit_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT ||
      receive_budget > NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(result, 0U, sizeof(*result));
  result->transmit_status = NPU_RUNTIME_SUCCESS;
  result->receive_status = NPU_RUNTIME_SUCCESS;

  if (transmit_budget != 0U) {
    result->transmit_status = plan_transmit_work(runtime, periodic_counter);
    while (result->transmit_status == NPU_RUNTIME_SUCCESS &&
           result->transmitted_packet_count < transmit_budget &&
           pending_packet_count(runtime) != 0U) {
      status = transmit_one_packet(runtime);
      if (status != NPU_RUNTIME_SUCCESS) {
        result->transmit_status = status;
        break;
      }
      ++result->transmitted_packet_count;
    }
  }

  while (result->received_packet_count < receive_budget) {
    status = receive_one_packet(runtime);
    if (status != NPU_RUNTIME_SUCCESS) {
      result->receive_status = status;
      break;
    }
    ++result->received_packet_count;
  }

  result->pending_transmit_packet_count = pending_packet_count(runtime);
  return NPU_RUNTIME_SUCCESS;
}

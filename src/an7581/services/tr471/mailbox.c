/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tr471/mailbox.h"

#include "an7581/runtime/endian.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/tr471/tdma.h"

#define NPU_TR471_COMMAND_SIZE sizeof(uint32_t)
#define NPU_TR471_CONFIGURE_MESSAGE_SIZE (6U * sizeof(uint32_t))
#define NPU_TR471_ETHERNET_MESSAGE_SIZE                                        \
  (NPU_TR471_COMMAND_SIZE + 2U * NPU_TR471_ETHERNET_ADDRESS_SIZE)
#define NPU_TR471_IPV4_MESSAGE_SIZE (4U * sizeof(uint32_t))
#define NPU_TR471_IPV6_MESSAGE_SIZE (10U * sizeof(uint32_t))
#define NPU_TR471_START_MESSAGE_SIZE (3U * sizeof(uint32_t))
#define NPU_TR471_RESULT_MESSAGE_SIZE                                          \
  (NPU_TR471_COMMAND_SIZE + NPU_TR471_RESULT_STATE_SIZE)
#define NPU_TR471_CLOCK_MESSAGE_SIZE (3U * sizeof(uint32_t))
#define NPU_TR471_BUFFER_ADDRESS_MESSAGE_SIZE (2U * sizeof(uint32_t))

_Static_assert(sizeof(struct npu_tr471_result_counters) ==
                   NPU_TR471_RESULT_STATE_SIZE,
               "TR-471 result ABI must remain 48 bytes");

#define NPU_TR471_DESTINATION_MAC_OFFSET 0U
#define NPU_TR471_SOURCE_MAC_OFFSET 6U
#define NPU_TR471_IPV4_SOURCE_ADDRESS_OFFSET 26U
#define NPU_TR471_IPV4_DESTINATION_ADDRESS_OFFSET 30U
#define NPU_TR471_IPV4_SOURCE_PORT_OFFSET 34U
#define NPU_TR471_IPV4_DESTINATION_PORT_OFFSET 36U
#define NPU_TR471_IPV6_SOURCE_ADDRESS_OFFSET 22U
#define NPU_TR471_IPV6_DESTINATION_ADDRESS_OFFSET 38U
#define NPU_TR471_IPV6_SOURCE_PORT_OFFSET 54U
#define NPU_TR471_IPV6_DESTINATION_PORT_OFFSET 56U

static const uint8_t ipv4_packet_template[NPU_TR471_IPV4_TEMPLATE_SIZE] = {
    0xa0U, 0x36U, 0x9fU, 0x54U, 0x61U, 0xc4U, 0x00U, 0xaaU, 0xbbU, 0x01U,
    0x23U, 0x40U, 0x08U, 0x00U, 0x45U, 0x00U, 0x04U, 0xccU, 0x00U, 0x01U,
    0x00U, 0x00U, 0x40U, 0x11U, 0x00U, 0x00U, 0xc0U, 0xa8U, 0x01U, 0x9bU,
    0xc0U, 0xa8U, 0x01U, 0x01U, 0x03U, 0xe8U, 0x03U, 0xe8U, 0x04U, 0xb8U,
    0x00U, 0x00U, 0xbeU, 0xefU, 0x00U, 0x00U, 0x00U, 0x00U,
};

static const uint8_t ipv6_packet_template[NPU_TR471_IPV6_TEMPLATE_SIZE] = {
    0xa0U, 0x36U, 0x9fU, 0x54U, 0x61U, 0xc4U, 0x00U, 0xaaU, 0xbbU, 0x01U,
    0x23U, 0x40U, 0x86U, 0xddU, 0x60U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x11U, 0xffU, 0x20U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x20U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x01U, 0x04U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U,
    0xb7U, 0xebU, 0xbeU, 0xefU, 0x00U, 0x00U, 0x00U, 0x00U,
};

static void store_little_endian_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static uint32_t clamp_payload_size(uint32_t payload_size) {
  if (payload_size > NPU_TR471_MAXIMUM_UDP_PAYLOAD_SIZE)
    return NPU_TR471_MAXIMUM_UDP_PAYLOAD_SIZE;

  return payload_size;
}

static bool configure_transmit(struct npu_tr471_state *state,
                               const uint8_t *message, size_t length) {
  struct npu_tr471_transmit_configuration configuration;

  if (length < NPU_TR471_CONFIGURE_MESSAGE_SIZE)
    return false;

  configuration.primary_packet_count = npu_load_little_endian_u32(message + 4U);
  configuration.primary_payload_size =
      clamp_payload_size(npu_load_little_endian_u32(message + 8U));
  configuration.burst_packet_count = npu_load_little_endian_u32(message + 12U);
  configuration.burst_payload_size =
      clamp_payload_size(npu_load_little_endian_u32(message + 16U));
  configuration.final_payload_size = npu_load_little_endian_u32(message + 20U);
  configuration.configured_at = state->clock;
  configuration.valid = true;
  state->transmit = configuration;
  return true;
}

static bool set_ethernet_addresses(struct npu_tr471_state *state,
                                   const uint8_t *message, size_t length) {
  if (length < NPU_TR471_ETHERNET_MESSAGE_SIZE)
    return false;

  (void)npu_memcpy(state->ethernet.destination,
                   message + NPU_TR471_COMMAND_SIZE,
                   sizeof(state->ethernet.destination));
  (void)npu_memcpy(state->ethernet.source,
                   message + NPU_TR471_COMMAND_SIZE +
                       NPU_TR471_ETHERNET_ADDRESS_SIZE,
                   sizeof(state->ethernet.source));
  state->ethernet.valid = true;
  return true;
}

static bool set_ipv4_flow(struct npu_tr471_state *state, const uint8_t *message,
                          size_t length) {
  struct npu_tr471_ipv4_flow flow = {0};

  if (length < NPU_TR471_IPV4_MESSAGE_SIZE)
    return false;

  (void)npu_memcpy(flow.destination_address, message + 4U,
                   sizeof(flow.destination_address));
  (void)npu_memcpy(flow.source_address, message + 8U,
                   sizeof(flow.source_address));
  flow.destination_port_wire = npu_load_little_endian_u16(message + 12U);
  flow.source_port_wire = npu_load_little_endian_u16(message + 14U);
  flow.valid = true;
  state->ipv4 = flow;
  state->selected_ip_version = NPU_TR471_IPV4;
  ++state->flow_revision;
  return true;
}

static bool set_ipv6_flow(struct npu_tr471_state *state, const uint8_t *message,
                          size_t length) {
  struct npu_tr471_ipv6_flow flow = {0};

  if (length < NPU_TR471_IPV6_MESSAGE_SIZE)
    return false;

  (void)npu_memcpy(flow.destination_address, message + 4U,
                   sizeof(flow.destination_address));
  (void)npu_memcpy(flow.source_address, message + 20U,
                   sizeof(flow.source_address));
  flow.destination_port_wire = npu_load_little_endian_u16(message + 36U);
  flow.source_port_wire = npu_load_little_endian_u16(message + 38U);
  flow.valid = true;
  state->ipv6 = flow;
  state->selected_ip_version = NPU_TR471_IPV6;
  ++state->flow_revision;
  return true;
}

static bool start_test(struct npu_tr471_state *state, const uint8_t *message,
                       size_t length) {
  if (length < NPU_TR471_START_MESSAGE_SIZE)
    return false;

  state->transmit_enabled = npu_load_little_endian_u32(message + 4U);
  state->selected_ip_version = npu_load_little_endian_u32(message + 8U);
  state->running = true;
  return true;
}

static void reset_test(struct npu_tr471_state *state) {
  (void)npu_memset(&state->transmit, 0U, sizeof(state->transmit));
  (void)npu_memset(&state->result.counters, 0U, sizeof(state->result.counters));
  state->result.valid = false;
  state->packet_sequence = 0U;
  state->last_received_sequence = 0U;
  (void)npu_memset(&state->reference_clock, 0U, sizeof(state->reference_clock));
  state->running = false;
}

static bool get_result_state(struct npu_tr471_state *state, uint8_t *message,
                             size_t length) {
  const struct npu_tr471_result_counters *counters = &state->result.counters;

  if (length < NPU_TR471_RESULT_MESSAGE_SIZE)
    return false;

  npu_store_little_endian_u32(message + 4U, counters->received_packet_count);
  npu_store_little_endian_u32(message + 8U, counters->received_payload_bytes);
  npu_store_little_endian_u32(message + 12U, counters->missing_packet_count);
  npu_store_little_endian_u32(message + 16U,
                              counters->out_of_order_packet_count);
  npu_store_little_endian_u32(message + 20U, counters->minimum_start_delay_ms);
  npu_store_little_endian_u32(message + 24U,
                              counters->start_delay_variation_ms);
  npu_store_little_endian_u32(message + 28U, counters->latency_valid);
  npu_store_little_endian_u32(message + 32U, counters->latest_latency_ms);
  npu_store_little_endian_u32(message + 36U, counters->reserved[0]);
  npu_store_little_endian_u32(message + 40U, counters->reserved[1]);
  npu_store_little_endian_u32(message + 44U, counters->reserved[2]);
  npu_store_little_endian_u32(message + 48U, counters->reserved[3]);
  state->reference_clock = state->clock;
  state->result.valid = true;
  state->result.reset_pending = true;
  return true;
}

static bool set_clock(struct npu_tr471_state *state, const uint8_t *message,
                      size_t length) {
  if (length < NPU_TR471_CLOCK_MESSAGE_SIZE)
    return false;

  state->clock.seconds = npu_load_little_endian_u32(message + 4U);
  state->clock.nanoseconds = npu_load_little_endian_u32(message + 8U);
  return true;
}

static bool get_clock(const struct npu_tr471_state *state, uint8_t *message,
                      size_t length) {
  if (length < NPU_TR471_CLOCK_MESSAGE_SIZE)
    return false;

  npu_store_little_endian_u32(message + 4U, state->clock.seconds);
  npu_store_little_endian_u32(message + 8U, state->clock.nanoseconds);
  return true;
}

static bool set_buffer_address(struct npu_tr471_state *state,
                               const uint8_t *message, size_t length) {
  uint32_t address;
  uint32_t physical_offset;

  if (length < NPU_TR471_BUFFER_ADDRESS_MESSAGE_SIZE)
    return false;

  address = npu_load_little_endian_u32(message + 4U);
  physical_offset = address & NPU_TR471_TDMA_BUFFER_ADDRESS_MASK;
  if ((address & (NPU_TR471_TDMA_PACKET_BUFFER_SIZE - 1U)) != 0U ||
      physical_offset > (NPU_TR471_TDMA_BUFFER_ADDRESS_MASK + 1U) -
                            NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT)
    return false;

  state->buffer_address = address;
  state->buffer_address_valid = true;
  ++state->buffer_revision;
  return true;
}

static bool record_result(struct npu_tr471_state *state, bool success) {
  ++state->decoded_requests;
  if (success)
    ++state->successful_requests;
  else
    ++state->rejected_requests;
  return success;
}

void npu_tr471_state_reset(struct npu_tr471_state *state) {
  if (state == NULL)
    return;

  (void)npu_memset(state, 0U, sizeof(*state));
}

bool npu_tr471_mailbox_handle(struct npu_tr471_state *state, void *buffer,
                              size_t length) {
  uint8_t *message = buffer;
  uint32_t command;
  bool success;

  if (state == NULL)
    return false;
  if (buffer == NULL || length < NPU_TR471_COMMAND_SIZE) {
    ++state->invalid_requests;
    return false;
  }

  command = npu_load_little_endian_u32(message);
  state->last_command = command;
  state->last_command_valid = true;
  switch (command) {
  case NPU_TR471_CONFIGURE_TRANSMIT:
    success = configure_transmit(state, message, length);
    break;
  case NPU_TR471_SET_ETHERNET_ADDRESSES:
    success = set_ethernet_addresses(state, message, length);
    break;
  case NPU_TR471_SET_IPV4_FLOW:
    success = set_ipv4_flow(state, message, length);
    break;
  case NPU_TR471_SET_IPV6_FLOW:
    success = set_ipv6_flow(state, message, length);
    break;
  case NPU_TR471_START_TEST:
    success = start_test(state, message, length);
    break;
  case NPU_TR471_RESET_TEST:
    reset_test(state);
    success = true;
    break;
  case NPU_TR471_GET_RESULT_STATE:
    success = get_result_state(state, message, length);
    break;
  case NPU_TR471_SET_CLOCK:
    success = set_clock(state, message, length);
    break;
  case NPU_TR471_GET_CLOCK:
    success = get_clock(state, message, length);
    break;
  case NPU_TR471_SET_BUFFER_ADDRESS:
    success = set_buffer_address(state, message, length);
    break;
  default:
    success = false;
    break;
  }

  return record_result(state, success);
}

bool npu_tr471_packet_template_build(const struct npu_tr471_state *state,
                                     uint8_t *output, size_t output_extent,
                                     size_t *template_size) {
  size_t size;

  if (state == NULL || output == NULL || template_size == NULL)
    return false;
  if (state->selected_ip_version == NPU_TR471_IPV4) {
    if (!state->ipv4.valid)
      return false;
    size = sizeof(ipv4_packet_template);
    if (output_extent < size)
      return false;
    (void)npu_memcpy(output, ipv4_packet_template, size);
    (void)npu_memcpy(output + NPU_TR471_IPV4_SOURCE_ADDRESS_OFFSET,
                     state->ipv4.source_address,
                     sizeof(state->ipv4.source_address));
    (void)npu_memcpy(output + NPU_TR471_IPV4_DESTINATION_ADDRESS_OFFSET,
                     state->ipv4.destination_address,
                     sizeof(state->ipv4.destination_address));
    store_little_endian_u16(output + NPU_TR471_IPV4_SOURCE_PORT_OFFSET,
                            state->ipv4.source_port_wire);
    store_little_endian_u16(output + NPU_TR471_IPV4_DESTINATION_PORT_OFFSET,
                            state->ipv4.destination_port_wire);
  } else {
    if (!state->ipv6.valid)
      return false;
    size = sizeof(ipv6_packet_template);
    if (output_extent < size)
      return false;
    (void)npu_memcpy(output, ipv6_packet_template, size);
    (void)npu_memcpy(output + NPU_TR471_IPV6_SOURCE_ADDRESS_OFFSET,
                     state->ipv6.source_address,
                     sizeof(state->ipv6.source_address));
    (void)npu_memcpy(output + NPU_TR471_IPV6_DESTINATION_ADDRESS_OFFSET,
                     state->ipv6.destination_address,
                     sizeof(state->ipv6.destination_address));
    store_little_endian_u16(output + NPU_TR471_IPV6_SOURCE_PORT_OFFSET,
                            state->ipv6.source_port_wire);
    store_little_endian_u16(output + NPU_TR471_IPV6_DESTINATION_PORT_OFFSET,
                            state->ipv6.destination_port_wire);
  }

  if (state->ethernet.valid) {
    (void)npu_memcpy(output + NPU_TR471_DESTINATION_MAC_OFFSET,
                     state->ethernet.destination,
                     sizeof(state->ethernet.destination));
    (void)npu_memcpy(output + NPU_TR471_SOURCE_MAC_OFFSET,
                     state->ethernet.source, sizeof(state->ethernet.source));
  }

  *template_size = size;
  return true;
}

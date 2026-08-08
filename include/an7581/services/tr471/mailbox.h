/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_MAILBOX_H
#define AN7581_TR471_MAILBOX_H

#include "an7581/platform/types.h"

#define NPU_TR471_ETHERNET_ADDRESS_SIZE 6U
#define NPU_TR471_IPV4_ADDRESS_SIZE 4U
#define NPU_TR471_IPV6_ADDRESS_SIZE 16U
#define NPU_TR471_RESULT_STATE_SIZE 48U
#define NPU_TR471_MAXIMUM_UDP_PAYLOAD_SIZE 1450U
#define NPU_TR471_IPV4_TEMPLATE_SIZE 48U
#define NPU_TR471_IPV6_TEMPLATE_SIZE 68U
#define NPU_TR471_MAXIMUM_TEMPLATE_SIZE NPU_TR471_IPV6_TEMPLATE_SIZE

enum npu_tr471_command {
  NPU_TR471_CONFIGURE_TRANSMIT = 1,
  NPU_TR471_SET_ETHERNET_ADDRESSES,
  NPU_TR471_SET_IPV4_FLOW,
  NPU_TR471_SET_IPV6_FLOW,
  NPU_TR471_START_TEST,
  NPU_TR471_RESET_TEST,
  NPU_TR471_GET_RESULT_STATE,
  NPU_TR471_SET_CLOCK,
  NPU_TR471_GET_CLOCK,
  NPU_TR471_SET_BUFFER_ADDRESS,
};

enum npu_tr471_ip_version {
  NPU_TR471_IPV4 = 0,
  NPU_TR471_IPV6 = 1,
};

struct npu_tr471_clock {
  uint32_t seconds;
  uint32_t nanoseconds;
};

struct npu_tr471_transmit_configuration {
  uint32_t primary_packet_count;
  uint32_t primary_payload_size;
  uint32_t burst_packet_count;
  uint32_t burst_payload_size;
  uint32_t final_payload_size;
  struct npu_tr471_clock configured_at;
  bool valid;
};

struct npu_tr471_ethernet_configuration {
  uint8_t destination[NPU_TR471_ETHERNET_ADDRESS_SIZE];
  uint8_t source[NPU_TR471_ETHERNET_ADDRESS_SIZE];
  bool valid;
};

struct npu_tr471_ipv4_flow {
  uint8_t destination_address[NPU_TR471_IPV4_ADDRESS_SIZE];
  uint8_t source_address[NPU_TR471_IPV4_ADDRESS_SIZE];
  uint16_t destination_port_wire;
  uint16_t source_port_wire;
  bool valid;
};

struct npu_tr471_ipv6_flow {
  uint8_t destination_address[NPU_TR471_IPV6_ADDRESS_SIZE];
  uint8_t source_address[NPU_TR471_IPV6_ADDRESS_SIZE];
  uint16_t destination_port_wire;
  uint16_t source_port_wire;
  bool valid;
};

struct npu_tr471_result_counters {
  uint32_t received_packet_count;
  uint32_t received_payload_bytes;
  uint32_t missing_packet_count;
  uint32_t out_of_order_packet_count;
  uint32_t minimum_start_delay_ms;
  uint32_t start_delay_variation_ms;
  uint32_t latency_valid;
  uint32_t latest_latency_ms;
  uint32_t reserved[4];
};

struct npu_tr471_result_state {
  struct npu_tr471_result_counters counters;
  bool valid;
  bool reset_pending;
};

struct npu_tr471_state {
  struct npu_tr471_transmit_configuration transmit;
  struct npu_tr471_ethernet_configuration ethernet;
  struct npu_tr471_ipv4_flow ipv4;
  struct npu_tr471_ipv6_flow ipv6;
  struct npu_tr471_result_state result;
  struct npu_tr471_clock clock;
  uint32_t buffer_address;
  uint32_t transmit_enabled;
  uint32_t selected_ip_version;
  uint32_t packet_sequence;
  uint32_t periodic_counter;
  uint32_t ten_millisecond_counter;
  uint32_t last_schedule_counter;
  uint32_t last_received_sequence;
  struct npu_tr471_clock reference_clock;
  uint32_t flow_revision;
  uint32_t buffer_revision;
  uint32_t last_command;
  uint32_t decoded_requests;
  uint32_t invalid_requests;
  uint32_t successful_requests;
  uint32_t rejected_requests;
  bool buffer_address_valid;
  bool running;
  bool last_command_valid;
};

void npu_tr471_state_reset(struct npu_tr471_state *state);
bool npu_tr471_mailbox_handle(struct npu_tr471_state *state, void *buffer,
                              size_t length);
bool npu_tr471_packet_template_build(const struct npu_tr471_state *state,
                                     uint8_t *output, size_t output_extent,
                                     size_t *template_size);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_BRIDGE_H
#define AN7581_TUNNEL_BRIDGE_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define NPU_TUNNEL_BRIDGE_CHANNEL_COUNT 8U
#define NPU_TUNNEL_BRIDGE_COMMAND_WORD_COUNT 7U
#define NPU_TUNNEL_BRIDGE_COMMAND_STRIDE_WORDS 8U
#define NPU_TUNNEL_BRIDGE_TX_CREDIT_MASK UINT32_C(0xff00)
#define NPU_TUNNEL_BRIDGE_TX_CREDIT_SHIFT 8U
#define NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_MASK UINT32_C(0x1fffffff)
#define NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_ALIAS UINT32_C(0x20000000)

typedef void (*npu_tunnel_bridge_barrier_fn)(void *context);
typedef enum npu_runtime_result (*npu_tunnel_bridge_packet_resolve_fn)(
    void *context, uint32_t packet_address, uint8_t **packet, size_t *extent);

struct npu_tunnel_packet_operation {
  uint32_t packet_address;
  uint32_t modifiers[4];
  uint16_t length;
  uint16_t source_offset;
  uint8_t channel;
  uint8_t operation;
  bool begin;
  bool end;
};

struct npu_tunnel_bridge_command {
  uint32_t words[NPU_TUNNEL_BRIDGE_COMMAND_WORD_COUNT];
};

struct npu_tunnel_bridge_backend {
  const volatile uint32_t *channel_status;
  size_t channel_status_word_count;
  volatile uint32_t *command_words;
  size_t command_word_count;
  size_t command_stride_words;
  npu_tunnel_bridge_barrier_fn barrier;
  void *barrier_context;
};

struct npu_tunnel_bridge_ingress_backend {
  const volatile uint32_t *channel_status;
  size_t channel_status_word_count;
  const volatile uint32_t *packet_addresses;
  size_t packet_address_word_count;
  size_t packet_address_stride_words;
  npu_tunnel_bridge_packet_resolve_fn resolve;
  void *resolve_context;
  uint32_t packet_address_alias;
};

struct npu_tunnel_bridge_ingress {
  struct npu_tunnel_bridge_ingress_backend backend;
  uint8_t cached_rx_count[NPU_TUNNEL_BRIDGE_CHANNEL_COUNT];
};

struct npu_tunnel_bridge_ingress_packet {
  uint8_t *packet;
  size_t packet_extent;
  uint32_t packet_address;
  uint32_t received_length;
  uint8_t channel;
};

struct npu_tunnel_bridge_packet_region {
  uint8_t *mapped_base;
  size_t mapped_extent;
  uint32_t packet_address_base;
};

bool npu_tunnel_bridge_packet_region_initialize(
    struct npu_tunnel_bridge_packet_region *region, uint8_t *mapped_base,
    size_t mapped_extent, uint32_t packet_address_base);
enum npu_runtime_result
npu_tunnel_bridge_packet_region_resolve(void *context, uint32_t packet_address,
                                        uint8_t **packet, size_t *extent);
bool npu_tunnel_bridge_ingress_initialize(
    struct npu_tunnel_bridge_ingress *ingress,
    const struct npu_tunnel_bridge_ingress_backend *backend);
enum npu_runtime_result npu_tunnel_bridge_ingress_dequeue(
    struct npu_tunnel_bridge_ingress *ingress, uint8_t channel,
    struct npu_tunnel_bridge_ingress_packet *packet);
bool npu_tunnel_packet_operation_set(
    struct npu_tunnel_packet_operation *operation, uint32_t packet_address,
    uint32_t length, uint32_t source_offset, uint8_t channel,
    uint8_t operation_type, bool begin, bool end);
uint32_t npu_tunnel_bridge_patch_u16(uint16_t offset, uint16_t value);
bool npu_tunnel_packet_operation_encode(
    const struct npu_tunnel_packet_operation *operation,
    struct npu_tunnel_bridge_command *command);
enum npu_runtime_result npu_tunnel_bridge_command_publish(
    const struct npu_tunnel_bridge_backend *backend, uint8_t channel,
    const struct npu_tunnel_bridge_command *command);
enum npu_runtime_result npu_tunnel_bridge_commands_publish(
    const struct npu_tunnel_bridge_backend *backend, uint8_t channel,
    const struct npu_tunnel_bridge_command *commands, size_t command_count);
enum npu_runtime_result npu_tunnel_bridge_packet_release(
    const struct npu_tunnel_bridge_backend *backend, uint8_t channel,
    uint32_t packet_address, uint16_t received_length);
enum npu_runtime_result
npu_tunnel_bridge_packet_release_callback(void *context, uint8_t channel,
                                          uint32_t packet_address,
                                          uint16_t received_length);

#endif

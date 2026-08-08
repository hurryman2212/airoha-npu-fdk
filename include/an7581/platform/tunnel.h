/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_H
#define AN7581_TUNNEL_H

#include "an7581/services/tunnel/runtime.h"
#include "an7581/services/wifi/region.h"

#define AN7581_TUNNEL_BRIDGE_BASE UINT32_C(0x1ec12000)
#define AN7581_TUNNEL_BRIDGE_CHANNEL_STATUS_BASE                               \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x50))
#define AN7581_TUNNEL_BRIDGE_RX_ADDRESS_BASE                                   \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x80))
#define AN7581_TUNNEL_BRIDGE_TX_COMMAND_BASE                                   \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x100))
#define AN7581_TUNNEL_BRIDGE_RX_ADDRESS_STRIDE_WORDS 4U
#define AN7581_TUNNEL_BRIDGE_PACKET_BASE_REGISTER                              \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x08))
#define AN7581_TUNNEL_BRIDGE_CONFIGURATION_REGISTER                            \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x10))
#define AN7581_TUNNEL_BRIDGE_ENABLE_REGISTER                                   \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x18))
#define AN7581_TUNNEL_BRIDGE_CHANNEL_INITIALIZE_BASE                           \
  (AN7581_TUNNEL_BRIDGE_BASE + UINT32_C(0x210))
#define AN7581_TUNNEL_BRIDGE_CHANNEL_INITIALIZE_STRIDE UINT32_C(0x10)
#define AN7581_TUNNEL_BRIDGE_CONFIGURATION_VALUE UINT32_C(0x00040800)
#define AN7581_TUNNEL_TEMPLATE_OFFSET UINT32_C(0x10000)
#define AN7581_TUNNEL_TEMPLATE_LIMIT UINT32_C(0x3e880000)
#define AN7581_TUNNEL_IPV6_FRAGMENT_TEMPLATE_OFFSET UINT32_C(0x0e00)
#define AN7581_TUNNEL_MAP_TEMPLATE_OFFSET UINT32_C(0x0e80)
#define AN7581_TUNNEL_SRV6_TEMPLATE_FIRST 20U

struct an7581_tunnel_platform_config {
  const volatile uint32_t *channel_status;
  size_t channel_status_word_count;
  const volatile uint32_t *rx_packet_addresses;
  size_t rx_packet_address_word_count;
  volatile uint32_t *tx_command_words;
  size_t tx_command_word_count;
  npu_tunnel_bridge_barrier_fn barrier;
  void *barrier_context;
  uint8_t *packet_region_mapping;
  size_t packet_region_extent;
  uint32_t packet_region_address;
  uint8_t *template_mapping;
  size_t template_extent;
  const struct npu_tunnel_state *mailbox_state;
  struct npu_tunnel_map_info_binding map_info;
  uint32_t ipv6_fragment_header_address;
  uint32_t udf_template_base_address;
};

struct an7581_tunnel_platform {
  struct npu_tunnel_bridge_backend bridge;
  struct npu_tunnel_bridge_packet_region packet_region;
  struct npu_tunnel_bridge_ingress ingress;
  struct npu_tunnel_packet_runtime runtime;
  uint8_t *template_mapping;
  size_t template_extent;
  bool initialized;
};

enum npu_runtime_result an7581_tunnel_platform_initialize(
    struct an7581_tunnel_platform *platform,
    const struct an7581_tunnel_platform_config *config);
enum npu_runtime_result an7581_tunnel_platform_mt7996_initialize(
    struct an7581_tunnel_platform *platform,
    struct npu_wifi_sram_allocator *allocator,
    const struct npu_tunnel_state *mailbox_state, uint32_t timer_clock_mhz);
enum npu_runtime_result an7581_tunnel_platform_synchronize_mailbox_state(
    struct an7581_tunnel_platform *platform);
enum npu_runtime_result
an7581_tunnel_platform_step(struct an7581_tunnel_platform *platform,
                            uint8_t channel,
                            struct npu_tunnel_packet_ingress_result *result);

#endif

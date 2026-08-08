/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/tunnel.h"

#include "an7581/platform/mmio.h"
#include "an7581/platform/timer.h"
#include "an7581/runtime/memory.h"

static void platform_rebind(struct an7581_tunnel_platform *platform) {
  platform->ingress.backend.resolve_context = &platform->packet_region;
  platform->runtime.config.bridge = &platform->bridge;
  platform->runtime.config.release_context = &platform->bridge;
  platform->runtime.reassembly.release_context = &platform->bridge;
}

static void tunnel_bridge_barrier(void *context) {
  (void)context;
  an7581_memory_barrier();
}

enum npu_runtime_result an7581_tunnel_platform_initialize(
    struct an7581_tunnel_platform *platform,
    const struct an7581_tunnel_platform_config *config) {
  struct an7581_tunnel_platform initialized = {0};
  struct npu_tunnel_packet_runtime_config runtime_config;
  struct npu_tunnel_bridge_ingress_backend ingress_backend;

  if (platform == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (config->tx_command_words == NULL || config->barrier == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->tx_command_word_count <
      NPU_TUNNEL_BRIDGE_CHANNEL_COUNT * NPU_TUNNEL_BRIDGE_COMMAND_STRIDE_WORDS)
    return NPU_RUNTIME_OUT_OF_RANGE;

  initialized.bridge = (struct npu_tunnel_bridge_backend){
      .channel_status = config->channel_status,
      .channel_status_word_count = config->channel_status_word_count,
      .command_words = config->tx_command_words,
      .command_word_count = config->tx_command_word_count,
      .command_stride_words = NPU_TUNNEL_BRIDGE_COMMAND_STRIDE_WORDS,
      .barrier = config->barrier,
      .barrier_context = config->barrier_context,
  };
  initialized.template_mapping = config->template_mapping;
  initialized.template_extent = config->template_extent;
  if (!npu_tunnel_bridge_packet_region_initialize(
          &initialized.packet_region, config->packet_region_mapping,
          config->packet_region_extent, config->packet_region_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  ingress_backend = (struct npu_tunnel_bridge_ingress_backend){
      .channel_status = config->channel_status,
      .channel_status_word_count = config->channel_status_word_count,
      .packet_addresses = config->rx_packet_addresses,
      .packet_address_word_count = config->rx_packet_address_word_count,
      .packet_address_stride_words =
          AN7581_TUNNEL_BRIDGE_RX_ADDRESS_STRIDE_WORDS,
      .resolve = npu_tunnel_bridge_packet_region_resolve,
      .resolve_context = &initialized.packet_region,
      .packet_address_alias = NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_ALIAS,
  };
  if (!npu_tunnel_bridge_ingress_initialize(&initialized.ingress,
                                            &ingress_backend))
    return NPU_RUNTIME_OUT_OF_RANGE;

  runtime_config = (struct npu_tunnel_packet_runtime_config){
      .bridge = &initialized.bridge,
      .mailbox_state = config->mailbox_state,
      .release = npu_tunnel_bridge_packet_release_callback,
      .release_context = &initialized.bridge,
      .map_info = config->map_info,
      .ipv6_fragment_header_address = config->ipv6_fragment_header_address,
      .udf_template_base_address = config->udf_template_base_address,
  };
  if (!npu_tunnel_packet_runtime_initialize(&initialized.runtime,
                                            &runtime_config))
    return NPU_RUNTIME_INVALID_ARGUMENT;

  initialized.initialized = true;
  *platform = initialized;
  platform_rebind(platform);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_tunnel_platform_mt7996_initialize(
    struct an7581_tunnel_platform *platform,
    struct npu_wifi_sram_allocator *allocator,
    const struct npu_tunnel_state *mailbox_state, uint32_t timer_clock_mhz) {
  struct an7581_tunnel_platform initialized = {0};
  struct an7581_tunnel_platform_config config;
  struct npu_wifi_region packet_region;
  enum npu_runtime_result status;
  uint32_t channel;
  uint32_t template_address;

  if (platform == NULL || allocator == NULL || mailbox_state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (timer_clock_mhz == 0U ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_TUNNEL_PACKETS, &packet_region) ||
      packet_region.address > UINT32_MAX - AN7581_TUNNEL_TEMPLATE_OFFSET)
    return NPU_RUNTIME_OUT_OF_RANGE;

  template_address = packet_region.address + AN7581_TUNNEL_TEMPLATE_OFFSET;
  if (packet_region.usable_size == 0U ||
      template_address >= AN7581_TUNNEL_TEMPLATE_LIMIT ||
      AN7581_TUNNEL_MAP_TEMPLATE_OFFSET >
          AN7581_TUNNEL_TEMPLATE_LIMIT - template_address ||
      NPU_TUNNEL_UDF_MAP_RECORD_SIZE > AN7581_TUNNEL_TEMPLATE_LIMIT -
                                           template_address -
                                           AN7581_TUNNEL_MAP_TEMPLATE_OFFSET)
    return NPU_RUNTIME_OUT_OF_RANGE;

  config = (struct an7581_tunnel_platform_config){
      .channel_status = (const volatile uint32_t *)(uintptr_t)
          AN7581_TUNNEL_BRIDGE_CHANNEL_STATUS_BASE,
      .channel_status_word_count = NPU_TUNNEL_BRIDGE_CHANNEL_COUNT,
      .rx_packet_addresses = (const volatile uint32_t *)(uintptr_t)
          AN7581_TUNNEL_BRIDGE_RX_ADDRESS_BASE,
      .rx_packet_address_word_count =
          NPU_TUNNEL_BRIDGE_CHANNEL_COUNT *
          AN7581_TUNNEL_BRIDGE_RX_ADDRESS_STRIDE_WORDS,
      .tx_command_words =
          (volatile uint32_t *)(uintptr_t)AN7581_TUNNEL_BRIDGE_TX_COMMAND_BASE,
      .tx_command_word_count = NPU_TUNNEL_BRIDGE_CHANNEL_COUNT *
                               NPU_TUNNEL_BRIDGE_COMMAND_STRIDE_WORDS,
      .barrier = tunnel_bridge_barrier,
      .packet_region_mapping = (uint8_t *)(uintptr_t)packet_region.address,
      .packet_region_extent = packet_region.usable_size,
      .packet_region_address = packet_region.address,
      .template_mapping = (uint8_t *)(uintptr_t)template_address,
      .template_extent = AN7581_TUNNEL_TEMPLATE_LIMIT - template_address,
      .mailbox_state = mailbox_state,
      .map_info =
          {
              .template_scratch =
                  (uint8_t *)(uintptr_t)(template_address +
                                         AN7581_TUNNEL_MAP_TEMPLATE_OFFSET),
              .template_scratch_extent = NPU_TUNNEL_UDF_MAP_RECORD_SIZE,
              .template_scratch_address =
                  template_address + AN7581_TUNNEL_MAP_TEMPLATE_OFFSET,
          },
      .ipv6_fragment_header_address =
          template_address + AN7581_TUNNEL_IPV6_FRAGMENT_TEMPLATE_OFFSET,
      .udf_template_base_address = template_address,
  };
  status = an7581_tunnel_platform_initialize(&initialized, &config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  if (!an7581_local_timer_configure(0U, 10U, timer_clock_mhz, true,
                                    AN7581_LOCAL_TIMER_PERIOD_10_MICROSECONDS,
                                    AN7581_LOCAL_TIMER_PRESERVE_WATCHDOG))
    return NPU_RUNTIME_IO_ERROR;

  an7581_mmio_write32(AN7581_TUNNEL_BRIDGE_PACKET_BASE_REGISTER,
                      packet_region.address &
                          NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_MASK);
  an7581_mmio_write32(AN7581_TUNNEL_BRIDGE_CONFIGURATION_REGISTER,
                      AN7581_TUNNEL_BRIDGE_CONFIGURATION_VALUE);
  an7581_mmio_write32(AN7581_TUNNEL_BRIDGE_ENABLE_REGISTER, 1U);
  if (!an7581_local_timer_delay_ms(10U, timer_clock_mhz))
    return NPU_RUNTIME_IO_ERROR;
  for (channel = 0U; channel < NPU_TUNNEL_BRIDGE_CHANNEL_COUNT; ++channel)
    an7581_mmio_write32(AN7581_TUNNEL_BRIDGE_CHANNEL_INITIALIZE_BASE +
                            channel *
                                AN7581_TUNNEL_BRIDGE_CHANNEL_INITIALIZE_STRIDE,
                        1U);

  *platform = initialized;
  platform_rebind(platform);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_tunnel_platform_synchronize_mailbox_state(
    struct an7581_tunnel_platform *platform) {
  const struct npu_tunnel_state *state;
  size_t index;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized ||
      platform->runtime.config.mailbox_state == NULL ||
      platform->template_mapping == NULL)
    return NPU_RUNTIME_REJECTED;

  state = platform->runtime.config.mailbox_state;
  for (index = 0U; index < NPU_TUNNEL_BRIDGE_CONTEXT_COUNT; ++index) {
    size_t offset = index * NPU_TUNNEL_UDF_TEMPLATE_STRIDE;

    if (state->bridge_context[index].valid) {
      if (offset > platform->template_extent ||
          NPU_TUNNEL_BRIDGE_CONTEXT_SIZE > platform->template_extent - offset)
        return NPU_RUNTIME_OUT_OF_RANGE;
      (void)npu_memcpy(platform->template_mapping + offset,
                       state->bridge_context[index].data,
                       NPU_TUNNEL_BRIDGE_CONTEXT_SIZE);
    }
  }
  for (index = 0U; index < NPU_TUNNEL_SRV6_HEADER_COUNT; ++index) {
    size_t offset = (AN7581_TUNNEL_SRV6_TEMPLATE_FIRST + index) *
                    NPU_TUNNEL_UDF_TEMPLATE_STRIDE;
    size_t length = state->srv6_header[index].length;

    if (state->srv6_header[index].valid) {
      if (offset > platform->template_extent ||
          length > platform->template_extent - offset)
        return NPU_RUNTIME_OUT_OF_RANGE;
      (void)npu_memcpy(platform->template_mapping + offset,
                       state->srv6_header[index].data, length);
    }
  }
  an7581_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_tunnel_platform_step(struct an7581_tunnel_platform *platform,
                            uint8_t channel,
                            struct npu_tunnel_packet_ingress_result *result) {
  if (platform == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_REJECTED;
  return npu_tunnel_packet_runtime_ingress_step(
      &platform->runtime, &platform->ingress, channel, result);
}

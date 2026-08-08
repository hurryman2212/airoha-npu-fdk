/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mailbox.h"

#include "an7581/runtime/endian.h"
#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/eagle_tx.h"

#define WLAN_HEADER_SIZE 8U
#define NPU_WIFI_MAX_HOST_ADDRESS UINT32_C(0xbfffffff)
#define NPU_WIFI_MT7996_SUPPORT_MAP_PHYSICAL_ADDRESS UINT32_C(0x1e800000)
#define NPU_WIFI_MT7996_SUPPORT_MAP_LOCAL_ADDRESS UINT32_C(0x3e800000)
#define NPU_WIFI_SUPPORT_MAP_READY UINT32_C(0x00000001)
#define NPU_WIFI_TOKEN_ID_SIZE_MINIMUM UINT32_C(0x000007ff)
#define NPU_WIFI_TOKEN_ID_SIZE_MAXIMUM UINT32_C(0x00007000)

static const uint8_t wifi_set_minimum_payload_size[NPU_WIFI_SET_COMMAND_COUNT] =
    {
        4U, 4U, 0U, 1U, 4U, 1U, 4U, 4U, 4U, 1U, 2U, 2U, 1U,  0U, 1U,  2U, 4U,
        1U, 1U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 2U, 0U, 4U, 12U, 0U, 32U, 4U, 4U,
};

typedef bool (*wifi_backend_address_operation)(void *context,
                                               uint32_t interface,
                                               uint32_t address);

static uint16_t load_little_endian_u16(const uint8_t *data) {
  return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8));
}

static void store_little_endian_u64(uint8_t *data, uint64_t value) {
  npu_store_little_endian_u32(data, (uint32_t)value);
  npu_store_little_endian_u32(data + sizeof(uint32_t), (uint32_t)(value >> 32));
}

static bool host_address_is_valid(uint32_t address) {
  return address <= NPU_WIFI_MAX_HOST_ADDRESS;
}

static void
interface_mark_valid(struct npu_wifi_interface_configuration *configuration,
                     uint32_t field) {
  configuration->valid_fields |= field;
}

static bool set_interface_address(
    uint32_t *destination,
    struct npu_wifi_interface_configuration *interface_configuration,
    uint32_t valid_field, uint32_t address,
    wifi_backend_address_operation operation, void *backend_context,
    uint32_t interface) {
  if (!host_address_is_valid(address) ||
      (operation != NULL && !operation(backend_context, interface, address)))
    return false;

  *destination = address;
  interface_mark_valid(interface_configuration, valid_field);
  return true;
}

bool npu_wifi_mail_set_inode_txrx_registers(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    const struct npu_wifi_inode_registers *registers) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || registers == NULL ||
      interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;
  interface_configuration = &configuration->interface[interface];
  if (configuration->backend == NULL) {
    if (interface == 0U) {
      uint32_t group = registers->direction;

      if (group >= NPU_WIFI_INODE_NORMAL_TABLE_GROUP_LIMIT)
        return false;
      configuration->inode_pending.normal_table_address[group] =
          registers->input_count_address;
      configuration->inode_pending.normal_table_valid[group / 32U] |=
          UINT32_C(1) << (group % 32U);
    } else if (interface == 1U) {
      configuration->inode_pending.special_table = *registers;
      configuration->inode_pending.special_table_valid = true;
    } else {
      return false;
    }
  } else if (configuration->backend->set_inode_txrx_registers != NULL &&
             !configuration->backend->set_inode_txrx_registers(
                 configuration->backend_context, interface, registers)) {
    return false;
  }

  if (interface == NPU_WIFI_INODE_STOP_ACTION)
    configuration->inode_stop_requested = true;
  else if (interface == NPU_WIFI_INODE_RESUME_ACTION)
    configuration->inode_stop_requested = false;

  interface_configuration->inode_registers = *registers;
  interface_mark_valid(interface_configuration,
                       NPU_WIFI_VALID_INODE_TXRX_REGISTERS);
  return true;
}

static bool apply_rate_limit(struct npu_wifi_configuration *configuration,
                             const uint8_t *payload) {
  uint32_t band = npu_load_little_endian_u32(payload);
  uint32_t bssid = npu_load_little_endian_u32(payload + 4U);

  if (band >= NPU_WIFI_RATE_LIMIT_BAND_COUNT || bssid >= NPU_WIFI_BSSID_COUNT)
    return false;

  configuration->rate_limit[band][bssid] =
      npu_load_little_endian_u32(payload + 8U);
  return true;
}

static bool
apply_arht_chip_information(struct npu_wifi_configuration *configuration,
                            const uint8_t *payload) {
  uint32_t gpio;
  uint32_t chip_id = npu_load_little_endian_u32(payload + 28U);

  if (chip_id == UINT32_MAX)
    return false;

  for (gpio = 0U; gpio < NPU_WIFI_ARHT_GPIO_COUNT; ++gpio)
    configuration->arht.gpio[gpio] =
        npu_load_little_endian_u32(payload + gpio * sizeof(uint32_t));
  configuration->arht.reserved = npu_load_little_endian_u32(payload + 24U);
  configuration->arht.chip_id = chip_id;
  configuration->arht.valid = true;
  return true;
}

bool npu_wifi_mail_initialize_rx_ring(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t descriptor_count) {
  const struct npu_wifi_rx_ring_profile *profile;
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;
  profile = npu_wifi_rx_ring_find_profile(interface);
  if (profile == NULL)
    return false;
  if (profile->kind == NPU_WIFI_RX_RING_IGNORED)
    return true;
  if (descriptor_count == 0U ||
      descriptor_count > profile->maximum_descriptor_count)
    return false;

  if (configuration->backend != NULL &&
      configuration->backend->initialize_rx_ring != NULL) {
    uint32_t rx_descriptor_base;

    if (!configuration->backend->initialize_rx_ring(
            configuration->backend_context, interface, descriptor_count,
            &rx_descriptor_base))
      return false;
    if (profile->publication_interface !=
            NPU_WIFI_RX_NO_PUBLICATION_INTERFACE &&
        !npu_wifi_publish_rx_descriptor_base(
            configuration, profile->publication_interface, rx_descriptor_base))
      return false;
  }

  interface_configuration = &configuration->interface[interface];
  interface_configuration->descriptor_count = descriptor_count;
  interface_mark_valid(interface_configuration,
                       NPU_WIFI_VALID_DESCRIPTOR_COUNT);
  return true;
}

bool npu_wifi_mail_set_transfer_to_cpu(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    bool transfer_to_cpu) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;
  interface_configuration = &configuration->interface[interface];
  interface_configuration->transfer_to_cpu = transfer_to_cpu ? 1U : 0U;
  interface_mark_valid(interface_configuration, NPU_WIFI_VALID_TRANSFER_TO_CPU);
  return true;
}

bool npu_wifi_mail_set_ba_window_size(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t window_size) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;
  interface_configuration = &configuration->interface[interface];
  interface_configuration->ba_window_size = window_size;
  interface_mark_valid(interface_configuration, NPU_WIFI_VALID_BA_WINDOW_SIZE);
  return true;
}

bool npu_wifi_mail_set_flush_one_timeout(
    struct npu_wifi_configuration *configuration, uint16_t timeout) {
  if (configuration == NULL)
    return false;
  configuration->flush_one_timeout = timeout;
  return true;
}

bool npu_wifi_mail_set_flush_all_timeout(
    struct npu_wifi_configuration *configuration, uint16_t timeout) {
  if (configuration == NULL)
    return false;
  configuration->flush_all_timeout = timeout;
  return true;
}

bool npu_wifi_mail_set_bar_information(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t information) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;
  interface_configuration = &configuration->interface[interface];
  interface_configuration->bar_information = information;
  interface_mark_valid(interface_configuration, NPU_WIFI_VALID_BAR_INFORMATION);
  return true;
}

bool npu_wifi_mail_set_fast_path_flag(
    struct npu_wifi_configuration *configuration, bool enabled) {
  if (configuration == NULL)
    return false;
  configuration->fast_path_enabled = enabled;
  return true;
}

bool npu_wifi_mail_set_tx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t address) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;
  interface_configuration = &configuration->interface[interface];
  return set_interface_address(
      &interface_configuration->tx_descriptor_base, interface_configuration,
      NPU_WIFI_VALID_TX_DESCRIPTOR_BASE, address,
      configuration->backend != NULL
          ? configuration->backend->set_tx_descriptor_base
          : NULL,
      configuration->backend_context, interface);
}

static bool apply_set(struct npu_wifi_configuration *configuration,
                      uint32_t interface, uint32_t command,
                      const uint8_t *payload, size_t payload_length) {
  struct npu_wifi_interface_configuration *interface_configuration =
      &configuration->interface[interface];
  uint32_t value;

  switch (command) {
  case NPU_WIFI_SET_PCIE_ADDR:
    return set_interface_address(
        &interface_configuration->pcie_address, interface_configuration,
        NPU_WIFI_VALID_PCIE_ADDRESS, npu_load_little_endian_u32(payload),
        configuration->backend != NULL
            ? configuration->backend->set_pcie_address
            : NULL,
        configuration->backend_context, interface);
  case NPU_WIFI_SET_DESC:
    return npu_wifi_mail_initialize_rx_ring(
        configuration, interface, npu_load_little_endian_u32(payload));
  case NPU_WIFI_SET_NPU_INIT_DONE:
    configuration->npu_init_done = true;
    return true;
  case NPU_WIFI_SET_TRAN_TO_CPU:
    if (payload[0] > 1U)
      return false;
    return npu_wifi_mail_set_transfer_to_cpu(configuration, interface,
                                             payload[0] != 0U);
  case NPU_WIFI_SET_BA_WIN_SIZE:
    return npu_wifi_mail_set_ba_window_size(
        configuration, interface, npu_load_little_endian_u32(payload));
  case NPU_WIFI_SET_DRIVER_MODEL:
    configuration->driver_model = payload[0];
    return true;
  case NPU_WIFI_SET_DEL_STA:
    value = npu_load_little_endian_u32(payload);
    if (configuration->backend != NULL &&
        configuration->backend->set_delete_station != NULL &&
        !configuration->backend->set_delete_station(
            configuration->backend_context, interface, value))
      return false;
    interface_configuration->delete_station = value;
    interface_mark_valid(interface_configuration,
                         NPU_WIFI_VALID_DELETE_STATION);
    return true;
  case NPU_WIFI_SET_DRAM_BA_NODE_ADDR:
    value = npu_load_little_endian_u32(payload);
    return npu_wifi_mail_set_dram_ba_node_address(configuration, value);
  case NPU_WIFI_SET_PKT_BUF_ADDR:
    value = npu_load_little_endian_u32(payload);
    return npu_wifi_mail_set_packet_buffer_address(configuration, value);
  case NPU_WIFI_SET_IS_TEST_NOBA:
    if (payload[0] > 1U)
      return false;
    configuration->test_no_ba = payload[0] != 0U;
    return true;
  case NPU_WIFI_SET_FLUSHONE_TIMEOUT:
    return npu_wifi_mail_set_flush_one_timeout(configuration,
                                               load_little_endian_u16(payload));
  case NPU_WIFI_SET_FLUSHALL_TIMEOUT:
    return npu_wifi_mail_set_flush_all_timeout(configuration,
                                               load_little_endian_u16(payload));
  case NPU_WIFI_SET_IS_FORCE_TO_CPU:
    if (payload[0] > 1U)
      return false;
    if (configuration->backend != NULL &&
        configuration->backend->set_force_to_cpu != NULL &&
        !configuration->backend->set_force_to_cpu(
            configuration->backend_context, payload[0] != 0U))
      return false;
    configuration->force_to_cpu = payload[0] != 0U;
    return true;
  case NPU_WIFI_SET_PCIE_STATE:
    interface_configuration->pcie_active = true;
    interface_mark_valid(interface_configuration, NPU_WIFI_VALID_PCIE_STATE);
    return true;
  case NPU_WIFI_SET_PCIE_PORT_TYPE:
    if (payload[0] >= NPU_WIFI_PCIE_PORT_TYPE_COUNT ||
        (configuration->backend != NULL &&
         configuration->backend->set_pcie_port_type != NULL &&
         !configuration->backend->set_pcie_port_type(
             configuration->backend_context, payload[0])))
      return false;
    configuration->pcie_port_type = payload[0];
    configuration->pcie_port_type_valid = true;
    return true;
  case NPU_WIFI_SET_ERROR_RETRY_TIMES:
    configuration->error_retry_count = load_little_endian_u16(payload);
    return true;
  case NPU_WIFI_SET_BAR_INFO:
    return npu_wifi_mail_set_bar_information(
        configuration, interface, npu_load_little_endian_u32(payload));
  case NPU_WIFI_SET_FAST_FLAG:
    if (payload[0] > 1U)
      return false;
    return npu_wifi_mail_set_fast_path_flag(configuration, payload[0] != 0U);
  case NPU_WIFI_SET_NPU_BAND0_ONCPU:
    if (payload[0] > 1U)
      return false;
    configuration->band0_on_cpu = payload[0] != 0U;
    return true;
  case NPU_WIFI_SET_TX_RING_PCIE_ADDR:
    return set_interface_address(
        &interface_configuration->tx_ring_pcie_address, interface_configuration,
        NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS,
        npu_load_little_endian_u32(payload),
        configuration->backend != NULL
            ? configuration->backend->set_tx_ring_pcie_address
            : NULL,
        configuration->backend_context, interface);
  case NPU_WIFI_SET_TX_DESC_HW_BASE:
    return npu_wifi_mail_set_tx_descriptor_base(
        configuration, interface, npu_load_little_endian_u32(payload));
  case NPU_WIFI_SET_TX_BUF_SPACE_HW_BASE:
    return set_interface_address(
        &interface_configuration->tx_buffer_space_base, interface_configuration,
        NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE,
        npu_load_little_endian_u32(payload),
        configuration->backend != NULL
            ? configuration->backend->set_tx_buffer_space_base
            : NULL,
        configuration->backend_context, interface);
  case NPU_WIFI_SET_RX_RING_FOR_TXDONE_HW_BASE:
    return set_interface_address(
        &interface_configuration->tx_done_ring_base, interface_configuration,
        NPU_WIFI_VALID_TX_DONE_RING_BASE, npu_load_little_endian_u32(payload),
        configuration->backend != NULL
            ? configuration->backend->set_tx_done_ring_base
            : NULL,
        configuration->backend_context, interface);
  case NPU_WIFI_SET_TX_PKT_BUF_ADDR:
    value = npu_load_little_endian_u32(payload);
    return npu_wifi_mail_set_tx_packet_buffer_address(configuration, value);
  case NPU_WIFI_SET_INODE_TXRX_REG_ADDR: {
    const struct npu_wifi_inode_registers registers = {
        .direction = npu_load_little_endian_u32(payload),
        .input_count_address = payload_length >= 8U
                                   ? npu_load_little_endian_u32(payload + 4U)
                                   : 0U,
        .output_status_address = payload_length >= 12U
                                     ? npu_load_little_endian_u32(payload + 8U)
                                     : 0U,
        .output_count_address = payload_length >= 16U
                                    ? npu_load_little_endian_u32(payload + 12U)
                                    : 0U,
    };

    return npu_wifi_mail_set_inode_txrx_registers(configuration, interface,
                                                  &registers);
  }
  case NPU_WIFI_SET_INODE_DEBUG_FLAG:
    value = npu_load_little_endian_u32(payload);
    interface_configuration->inode_debug_flag = value;
    interface_mark_valid(interface_configuration,
                         NPU_WIFI_VALID_INODE_DEBUG_FLAG);
    npu_wifi_inode_debug_flag_set_wrapper(
        &configuration->compatibility_diagnostics, interface, value);
    return true;
  case NPU_WIFI_SET_INODE_HW_CFG_INFO:
    return npu_wifi_mail_set_inode_hardware_config(configuration, interface,
                                                   payload[0], payload[1]);
  case NPU_WIFI_SET_INODE_STOP_ACTION:
    return npu_wifi_mail_set_inode_stop_action(configuration, interface);
  case NPU_WIFI_SET_INODE_PCIE_SWAP:
    value = npu_load_little_endian_u32(payload);
    return npu_wifi_mail_set_inode_pcie_swap(configuration, interface, value);
  case NPU_WIFI_SET_RATELIMIT_CTRL:
    return apply_rate_limit(configuration, payload);
  case NPU_WIFI_SET_HWNAT_INIT:
    configuration->hwnat_initialized = true;
    return true;
  case NPU_WIFI_SET_ARHT_CHIP_INFO:
    return apply_arht_chip_information(configuration, payload);
  case NPU_WIFI_SET_TX_BUF_CHECK_ADDR:
    value = npu_load_little_endian_u32(payload);
    if (!host_address_is_valid(value))
      return false;
    configuration->tx_buffer_check_address = value;
    configuration->tx_buffer_check_address_valid = true;
    return true;
  case NPU_WIFI_SET_TOKEN_ID_SIZE:
    value = npu_load_little_endian_u32(payload);
    if (value < NPU_WIFI_TOKEN_ID_SIZE_MINIMUM ||
        value > NPU_WIFI_TOKEN_ID_SIZE_MAXIMUM)
      return false;
    configuration->token_id_size = value;
    configuration->token_id_size_valid = true;
    return true;
  default:
    return false;
  }
}

static bool handle_set(struct npu_wifi_configuration *configuration,
                       uint32_t interface, uint32_t command,
                       const uint8_t *payload, size_t payload_length) {
  if (command >= NPU_WIFI_SET_COMMAND_COUNT ||
      payload_length < wifi_set_minimum_payload_size[command])
    return false;

  return apply_set(configuration, interface, command, payload, payload_length);
}

static bool handle_get(struct npu_wifi_configuration *configuration,
                       uint32_t interface, uint32_t command, uint8_t *payload,
                       size_t payload_length) {
  const struct npu_wifi_interface_configuration *interface_configuration =
      &configuration->interface[interface];
  uint32_t value;

  switch (command) {
  case NPU_WIFI_GET_NPU_INFO:
    if (payload_length < sizeof(uint32_t))
      return false;
    value = 0U;
    (void)npu_wifi_npu_information_query(&configuration->npu_information,
                                         interface, &value);
    npu_store_little_endian_u32(payload, value);
    return true;
  case NPU_WIFI_GET_LAST_RATE:
    if (payload_length < sizeof(configuration->last_rate))
      return false;
    npu_store_little_endian_u32(payload, configuration->last_rate[0]);
    npu_store_little_endian_u32(payload + sizeof(uint32_t),
                                configuration->last_rate[1]);
    return true;
  case NPU_WIFI_GET_COUNTER:
    return npu_wifi_get_counter(configuration, payload, payload_length);
  case NPU_WIFI_GET_DBG_COUNTER:
    if (payload_length < sizeof(uint32_t))
      return false;
    npu_store_little_endian_u32(payload,
                                interface_configuration->debug_counter_address);
    return true;
  case NPU_WIFI_GET_RXDESC_BASE:
    if (payload_length < sizeof(uint32_t) ||
        !npu_wifi_mail_get_rx_descriptor_base(configuration, interface, &value))
      return false;
    npu_store_little_endian_u32(payload, value);
    return true;
  case NPU_WIFI_GET_WCID_DBG_COUNTER:
    if (payload_length < sizeof(uint32_t))
      return false;
    npu_store_little_endian_u32(
        payload, npu_wifi_wcid_debug_counter_get_wrapper(
                     &configuration->compatibility_diagnostics, interface));
    return true;
  case NPU_WIFI_GET_DMA_ADDR:
    if (payload_length < sizeof(uint32_t) ||
        !npu_wifi_mail_get_dma_address(configuration,
                                       npu_load_little_endian_u32(payload),
                                       interface, &value))
      return false;
    npu_store_little_endian_u32(payload, value);
    return true;
  case NPU_WIFI_GET_RING_SIZE:
    if (payload_length < sizeof(uint32_t) ||
        !npu_wifi_mail_get_ring_size(configuration,
                                     npu_load_little_endian_u32(payload),
                                     interface, &value))
      return false;
    npu_store_little_endian_u32(payload, value);
    return true;
  case NPU_WIFI_GET_NPU_SUPPORT_MAP:
    return npu_wifi_get_support_map(configuration, payload, payload_length);
  case NPU_WIFI_GET_MDC_LOCK_ADDRESS:
    if (payload_length < sizeof(uint32_t))
      return false;
    npu_store_little_endian_u32(payload, 0U);
    return true;
  case NPU_WIFI_GET_NPU_VERSION:
    if (payload_length < sizeof(uint32_t))
      return false;
    npu_store_little_endian_u32(payload,
                                npu_wifi_query_version_response(configuration));
    return true;
  default:
    return false;
  }
}

static void record_request(struct npu_wifi_configuration *configuration,
                           uint32_t interface, uint32_t operation,
                           uint32_t command, size_t payload_length) {
  configuration->last_request.interface = interface;
  configuration->last_request.operation = operation;
  configuration->last_request.command = command;
  configuration->last_request.payload_length = (uint32_t)payload_length;
  configuration->last_request_valid = true;
}

static bool request_result(struct npu_wifi_configuration *configuration,
                           bool success) {
  ++configuration->decoded_requests;
  if (success)
    ++configuration->successful_requests;
  else
    ++configuration->rejected_requests;
  return success;
}

static bool handle_wifi(struct npu_firmware_state *state, void *buffer,
                        size_t length) {
  struct npu_wifi_configuration *configuration = &state->wifi;
  uint8_t *bytes = buffer;
  uint32_t metadata;
  uint32_t interface;
  uint32_t operation;
  uint32_t command;
  size_t payload_length;
  bool success;

  if (buffer == NULL || length < WLAN_HEADER_SIZE) {
    ++configuration->invalid_requests;
    return false;
  }

  metadata = npu_load_little_endian_u32(bytes);
  interface = metadata & UINT32_C(0x0f);
  operation = (metadata >> 4) & UINT32_C(0x0f);
  command = npu_load_little_endian_u32(bytes + sizeof(uint32_t));
  payload_length = length - WLAN_HEADER_SIZE;
  record_request(configuration, interface, operation, command, payload_length);

  switch (operation) {
  case NPU_WIFI_OPERATION_SET:
    success = handle_set(configuration, interface, command,
                         bytes + WLAN_HEADER_SIZE, payload_length);
    return request_result(configuration, success);
  case NPU_WIFI_OPERATION_SET_NO_WAIT:
  case NPU_WIFI_OPERATION_GET_NO_WAIT:
    if (command != 0U) {
      ++configuration->invalid_requests;
      return false;
    }
    return request_result(configuration, true);
  case NPU_WIFI_OPERATION_GET:
    if (command >= NPU_WIFI_GET_COMMAND_COUNT) {
      ++configuration->invalid_requests;
      return false;
    }
    success = handle_get(configuration, interface, command,
                         bytes + WLAN_HEADER_SIZE, payload_length);
    return request_result(configuration, success);
  default:
    ++configuration->invalid_requests;
    return false;
  }
}

void npu_firmware_state_reset(struct npu_firmware_state *state) {
  size_t family;
  size_t interface;

  if (state == NULL)
    return;

  (void)npu_memset(state, 0U, sizeof(*state));
  state->wifi.support_map = (struct npu_wifi_support_map_binding){
      .memory = (uint8_t *)(uintptr_t)NPU_WIFI_MT7996_SUPPORT_MAP_LOCAL_ADDRESS,
      .extent = sizeof(uint32_t),
      .physical_address = NPU_WIFI_MT7996_SUPPORT_MAP_PHYSICAL_ADDRESS,
      .active = true,
  };
  npu_tr471_state_reset(&state->tr471);
  for (interface = 0U; interface < NPU_WIFI_INTERFACE_COUNT; ++interface) {
    for (family = 0U; family < NPU_WIFI_COUNTER_FLAG_FAMILY_COUNT; ++family)
      state->wifi.counter_snapshot.interface[interface].flag[family] =
          UINT8_MAX;
  }
  state->outer_function_mask =
      NPU_OUTER_FUNCTION_MASK(NPU_OUTER_FUNCTION_WIFI) |
      NPU_OUTER_FUNCTION_MASK(NPU_OUTER_FUNCTION_TUNNEL) |
      NPU_OUTER_FUNCTION_MASK(NPU_OUTER_FUNCTION_TR471) |
      NPU_OUTER_FUNCTION_MASK(NPU_OUTER_FUNCTION_PPE);
  state->wifi.last_rate[0] = UINT32_C(0x000000de);
  state->wifi.last_rate[1] = UINT32_C(0x00000d05);
  /*
   * The vendor parser expects a dash-delimited version.  The embedded
   * TLB7.7.0.0_v03 string has no dash, so this exact MT7996 image reports
   * the common unsupported-query sentinel.
   */
  state->wifi.version_response = NPU_WIFI_UNSUPPORTED_QUERY_RESPONSE;
}

void npu_wifi_configuration_set_backend(
    struct npu_wifi_configuration *configuration,
    const struct npu_wifi_backend_operations *backend, void *backend_context) {
  if (configuration == NULL)
    return;

  configuration->backend = backend;
  configuration->backend_context = backend_context;
}

__attribute__((weak)) uint32_t npu_wifi_query_version_response(
    const struct npu_wifi_configuration *configuration) {
  return configuration != NULL ? configuration->version_response
                               : NPU_WIFI_UNSUPPORTED_QUERY_RESPONSE;
}

bool npu_wifi_get_counter(const struct npu_wifi_configuration *configuration,
                          uint8_t *payload, size_t payload_length) {
  size_t interface_count;
  size_t response_size;
  size_t counter_bytes;
  size_t counter_offset;
  size_t flag_offset;
  size_t family;
  size_t interface;

  if (configuration == NULL || payload == NULL)
    return false;

  interface_count = NPU_WIFI_INTERFACE_COUNT;
  response_size = NPU_WIFI_MT7996_COUNTER_RESPONSE_SIZE;
  if (payload_length < response_size)
    return false;

  counter_bytes = interface_count * sizeof(uint64_t);
  counter_offset = NPU_WIFI_COUNTER_RESERVED_PAYLOAD_SIZE;
  for (family = 0U; family < NPU_WIFI_COUNTER_FAMILY_COUNT; ++family) {
    for (interface = 0U; interface < interface_count; ++interface) {
      uint64_t value = 0U;

      value =
          configuration->counter_snapshot.interface[interface].counter[family];
      store_little_endian_u64(payload + counter_offset +
                                  family * counter_bytes +
                                  interface * sizeof(uint64_t),
                              value);
    }
  }

  flag_offset = counter_offset + NPU_WIFI_COUNTER_FAMILY_COUNT * counter_bytes;
  for (family = 0U; family < NPU_WIFI_COUNTER_FLAG_FAMILY_COUNT; ++family) {
    for (interface = 0U; interface < interface_count; ++interface) {
      uint8_t value = 0U;

      value = configuration->counter_snapshot.interface[interface].flag[family];
      payload[flag_offset + family * interface_count + interface] = value;
    }
  }

  (void)npu_memset(payload + flag_offset +
                       NPU_WIFI_COUNTER_FLAG_FAMILY_COUNT * interface_count,
                   0U, NPU_WIFI_COUNTER_TRAILING_WORD_COUNT * sizeof(uint32_t));
  return true;
}

bool npu_wifi_get_support_map(struct npu_wifi_configuration *configuration,
                              uint8_t *payload, size_t payload_length) {
  struct npu_wifi_support_map_binding *binding;

  if (configuration == NULL || payload == NULL)
    return false;

  if (payload_length < NPU_WIFI_MT7996_SUPPORT_MAP_RESPONSE_SIZE)
    return false;

  binding = &configuration->support_map;
  if (!binding->active || binding->memory == NULL ||
      binding->extent < sizeof(uint32_t))
    return false;

  npu_store_little_endian_u32(
      payload + NPU_WIFI_MT7996_SUPPORT_MAP_ADDRESS_OFFSET,
      binding->physical_address & NPU_WIFI_PHYSICAL_ADDRESS_MASK);
  npu_store_little_endian_u32(binding->memory, NPU_WIFI_SUPPORT_MAP_READY);
  return true;
}

bool npu_wifi_mail_set_inode_hardware_config(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint8_t endpoint_mask, uint8_t vap_mask) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;

  interface_configuration = &configuration->interface[interface];
  interface_configuration->inode_endpoint_mask = endpoint_mask;
  interface_configuration->inode_vap_mask = vap_mask;
  interface_mark_valid(interface_configuration,
                       NPU_WIFI_VALID_INODE_HARDWARE_CONFIG);
  npu_wifi_inode_hardware_config_set_wrapper(
      &configuration->compatibility_diagnostics, interface, endpoint_mask,
      vap_mask);
  return true;
}

bool npu_wifi_mail_set_inode_stop_action(
    struct npu_wifi_configuration *configuration, uint32_t interface) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;

  interface_configuration = &configuration->interface[interface];
  configuration->inode_stop_requested = true;
  interface_mark_valid(interface_configuration,
                       NPU_WIFI_VALID_INODE_STOP_ACTION);
  npu_wifi_inode_stop_set_wrapper(&configuration->compatibility_diagnostics,
                                  interface);
  return true;
}

bool npu_wifi_mail_set_inode_pcie_swap(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t value) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;

  interface_configuration = &configuration->interface[interface];
  interface_configuration->inode_pcie_swap = value;
  interface_mark_valid(interface_configuration, NPU_WIFI_VALID_INODE_PCIE_SWAP);
  npu_wifi_inode_pcie_swap_set_wrapper(
      &configuration->compatibility_diagnostics, value);
  return true;
}

bool npu_wifi_mail_get_ring_size(
    const struct npu_wifi_configuration *configuration, uint32_t direction,
    uint32_t interface, uint32_t *ring_size) {
  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT ||
      ring_size == NULL)
    return false;

  return npu_wifi_ring_size_get_wrapper(
      &configuration->compatibility_diagnostics, direction, interface,
      ring_size);
}

bool npu_wifi_mail_get_dma_address(
    const struct npu_wifi_configuration *configuration, uint32_t direction,
    uint32_t interface, uint32_t *dma_address) {
  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT ||
      dma_address == NULL)
    return false;

  return npu_wifi_ring_dma_address_get_wrapper(
      &configuration->compatibility_diagnostics, direction, interface,
      dma_address);
}

bool npu_wifi_mail_get_rx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t *descriptor_base) {
  const struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT ||
      descriptor_base == NULL)
    return false;

  if (configuration->backend != NULL &&
      configuration->backend->prepare_rx_descriptor_base != NULL &&
      !configuration->backend->prepare_rx_descriptor_base(
          configuration->backend_context, interface))
    return false;

  interface_configuration = &configuration->interface[interface];
  if ((interface_configuration->valid_fields &
       NPU_WIFI_VALID_RX_DESCRIPTOR_BASE) == 0U ||
      interface_configuration->rx_descriptor_base == 0U) {
    *descriptor_base = NPU_WIFI_UNSUPPORTED_QUERY_RESPONSE;
    return true;
  }

  *descriptor_base = interface_configuration->rx_descriptor_base;
  return true;
}

bool npu_wifi_mail_set_packet_buffer_address(
    struct npu_wifi_configuration *configuration, uint32_t address) {
  if (configuration == NULL)
    return false;

  configuration->packet_buffer_address = address;
  configuration->packet_buffer_address_valid = true;
  configuration->packet_buffer_address_out_of_range =
      !host_address_is_valid(address);
  return true;
}

bool npu_wifi_mail_set_tx_packet_buffer_address(
    struct npu_wifi_configuration *configuration, uint32_t address) {
  if (configuration == NULL)
    return false;

  configuration->tx_packet_buffer_address = address;
  configuration->tx_packet_buffer_address_valid = true;
  configuration->tx_packet_buffer_address_out_of_range =
      !host_address_is_valid(address);
  return true;
}

bool npu_wifi_mail_set_dram_ba_node_address(
    struct npu_wifi_configuration *configuration, uint32_t address) {
  if (configuration == NULL || !host_address_is_valid(address))
    return false;
  if (configuration->backend != NULL &&
      configuration->backend->set_dram_ba_node_address != NULL &&
      !configuration->backend->set_dram_ba_node_address(
          configuration->backend_context, address))
    return false;

  configuration->dram_ba_node_address = address;
  configuration->dram_ba_node_address_valid = true;
  return true;
}

bool npu_wifi_publish_rx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t physical_address) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT ||
      !host_address_is_valid(physical_address))
    return false;

  interface_configuration = &configuration->interface[interface];
  interface_configuration->rx_descriptor_base =
      physical_address & NPU_WIFI_PHYSICAL_ADDRESS_MASK;
  interface_mark_valid(interface_configuration,
                       NPU_WIFI_VALID_RX_DESCRIPTOR_BASE);
  return true;
}

bool npu_wifi_unpublish_rx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface) {
  struct npu_wifi_interface_configuration *interface_configuration;

  if (configuration == NULL || interface >= NPU_WIFI_INTERFACE_COUNT)
    return false;

  interface_configuration = &configuration->interface[interface];
  interface_configuration->rx_descriptor_base = 0U;
  interface_configuration->valid_fields &=
      ~(uint32_t)NPU_WIFI_VALID_RX_DESCRIPTOR_BASE;
  return true;
}

bool npu_mailbox_dispatch(struct npu_firmware_state *state,
                          uint32_t outer_function, void *buffer, size_t length,
                          bool *request_applied) {
  bool service_success;

  if (request_applied == NULL)
    return false;
  *request_applied = false;
  if (state == NULL || outer_function > NPU_OUTER_FUNCTION_PPE ||
      (state->outer_function_mask & NPU_OUTER_FUNCTION_MASK(outer_function)) ==
          0U)
    return false;

  if (outer_function == NPU_OUTER_FUNCTION_WIFI) {
    service_success = handle_wifi(state, buffer, length);
    *request_applied = service_success;
    return service_success;
  }

  if (outer_function == NPU_OUTER_FUNCTION_TUNNEL) {
    service_success = npu_tunnel_mailbox_handle(&state->tunnel, buffer, length);
    *request_applied = service_success;
    return service_success;
  }

  if (outer_function == NPU_OUTER_FUNCTION_TR471) {
    /*
     * The original MT7996 dispatcher applies TR-471 commands but returns zero
     * for every command, so the mailbox wire status remains error. Preserve
     * that legacy boundary behavior without discarding the service-level
     * success result used by direct SDK callers.
     */
    *request_applied = npu_tr471_mailbox_handle(&state->tr471, buffer, length);
    return false;
  }

  if (outer_function != NPU_OUTER_FUNCTION_PPE)
    return false;

  service_success = npu_ppe_mailbox_handle(&state->ppe, buffer, length);
  *request_applied = service_success;
  return service_success;
}

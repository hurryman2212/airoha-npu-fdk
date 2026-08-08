/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MAILBOX_H
#define AN7581_WIFI_MAILBOX_H

#include "an7581/platform/types.h"
#include "an7581/services/ppe/mailbox.h"
#include "an7581/services/tr471/mailbox.h"
#include "an7581/services/tunnel/mailbox.h"
#include "an7581/services/wifi/compatibility.h"
#include "an7581/services/wifi/npu_information.h"
#include "an7581/services/wifi/rx_ring.h"

#define NPU_WIFI_INTERFACE_COUNT 16U
#define NPU_WIFI_SET_COMMAND_COUNT 34U
#define NPU_WIFI_GET_COMMAND_COUNT 11U
#define NPU_WIFI_RATE_LIMIT_BAND_COUNT 3U
#define NPU_WIFI_BSSID_COUNT 16U
#define NPU_WIFI_ARHT_GPIO_COUNT 6U
#define NPU_WIFI_MT7996_SUPPORT_MAP_RESPONSE_SIZE 28U
#define NPU_WIFI_MT7996_SUPPORT_MAP_ADDRESS_OFFSET 24U
#define NPU_WIFI_COUNTER_FAMILY_COUNT 4U
#define NPU_WIFI_COUNTER_FLAG_FAMILY_COUNT 2U
#define NPU_WIFI_COUNTER_TRAILING_WORD_COUNT 8U
#define NPU_WIFI_COUNTER_RESERVED_PAYLOAD_SIZE 8U
#define NPU_WIFI_MT7996_COUNTER_RESPONSE_SIZE 584U
#define NPU_WIFI_UNSUPPORTED_QUERY_RESPONSE UINT32_C(0x00000457)
#define NPU_WIFI_PHYSICAL_ADDRESS_MASK UINT32_C(0x1fffffff)
#define NPU_WIFI_INODE_NORMAL_TABLE_GROUP_LIMIT UINT32_C(512)
#define NPU_WIFI_INODE_NORMAL_TABLE_VALID_WORD_COUNT                           \
  (NPU_WIFI_INODE_NORMAL_TABLE_GROUP_LIMIT / 32U)
#define NPU_WIFI_INODE_STOP_ACTION UINT32_C(4)
#define NPU_WIFI_INODE_RESUME_ACTION UINT32_C(7)

#define NPU_FIRMWARE_VERSION_MAJOR 7U
#define NPU_FIRMWARE_VERSION_MINOR 7U
enum npu_outer_function {
  NPU_OUTER_FUNCTION_WIFI = 0,
  NPU_OUTER_FUNCTION_TUNNEL,
  NPU_OUTER_FUNCTION_NOTIFY,
  NPU_OUTER_FUNCTION_DBA,
  NPU_OUTER_FUNCTION_TR471,
  NPU_OUTER_FUNCTION_PPE,
};

#define NPU_OUTER_FUNCTION_MASK(function) (UINT32_C(1) << (function))

enum npu_wifi_operation {
  NPU_WIFI_OPERATION_SET = 1,
  NPU_WIFI_OPERATION_SET_NO_WAIT,
  NPU_WIFI_OPERATION_GET,
  NPU_WIFI_OPERATION_GET_NO_WAIT,
};

enum npu_wifi_set_command {
  NPU_WIFI_SET_PCIE_ADDR = 0,
  NPU_WIFI_SET_DESC,
  NPU_WIFI_SET_NPU_INIT_DONE,
  NPU_WIFI_SET_TRAN_TO_CPU,
  NPU_WIFI_SET_BA_WIN_SIZE,
  NPU_WIFI_SET_DRIVER_MODEL,
  NPU_WIFI_SET_DEL_STA,
  NPU_WIFI_SET_DRAM_BA_NODE_ADDR,
  NPU_WIFI_SET_PKT_BUF_ADDR,
  NPU_WIFI_SET_IS_TEST_NOBA,
  NPU_WIFI_SET_FLUSHONE_TIMEOUT,
  NPU_WIFI_SET_FLUSHALL_TIMEOUT,
  NPU_WIFI_SET_IS_FORCE_TO_CPU,
  NPU_WIFI_SET_PCIE_STATE,
  NPU_WIFI_SET_PCIE_PORT_TYPE,
  NPU_WIFI_SET_ERROR_RETRY_TIMES,
  NPU_WIFI_SET_BAR_INFO,
  NPU_WIFI_SET_FAST_FLAG,
  NPU_WIFI_SET_NPU_BAND0_ONCPU,
  NPU_WIFI_SET_TX_RING_PCIE_ADDR,
  NPU_WIFI_SET_TX_DESC_HW_BASE,
  NPU_WIFI_SET_TX_BUF_SPACE_HW_BASE,
  NPU_WIFI_SET_RX_RING_FOR_TXDONE_HW_BASE,
  NPU_WIFI_SET_TX_PKT_BUF_ADDR,
  NPU_WIFI_SET_INODE_TXRX_REG_ADDR,
  NPU_WIFI_SET_INODE_DEBUG_FLAG,
  NPU_WIFI_SET_INODE_HW_CFG_INFO,
  NPU_WIFI_SET_INODE_STOP_ACTION,
  NPU_WIFI_SET_INODE_PCIE_SWAP,
  NPU_WIFI_SET_RATELIMIT_CTRL,
  NPU_WIFI_SET_HWNAT_INIT,
  NPU_WIFI_SET_ARHT_CHIP_INFO,
  NPU_WIFI_SET_TX_BUF_CHECK_ADDR,
  NPU_WIFI_SET_TOKEN_ID_SIZE,
};

enum npu_wifi_get_command {
  NPU_WIFI_GET_NPU_INFO = 0,
  NPU_WIFI_GET_LAST_RATE,
  NPU_WIFI_GET_COUNTER,
  NPU_WIFI_GET_DBG_COUNTER,
  NPU_WIFI_GET_RXDESC_BASE,
  NPU_WIFI_GET_WCID_DBG_COUNTER,
  NPU_WIFI_GET_DMA_ADDR,
  NPU_WIFI_GET_RING_SIZE,
  NPU_WIFI_GET_NPU_SUPPORT_MAP,
  NPU_WIFI_GET_MDC_LOCK_ADDRESS,
  NPU_WIFI_GET_NPU_VERSION,
};

enum npu_wifi_band {
  NPU_WIFI_BAND_0 = 0,
  NPU_WIFI_BAND_1,
  NPU_WIFI_BAND_2,
  NPU_WIFI_BAND_COUNT,
};

enum npu_wifi_interface_valid_field {
  NPU_WIFI_VALID_PCIE_ADDRESS = UINT32_C(1) << 0,
  NPU_WIFI_VALID_DESCRIPTOR_COUNT = UINT32_C(1) << 1,
  NPU_WIFI_VALID_RX_DESCRIPTOR_BASE = UINT32_C(1) << 2,
  NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS = UINT32_C(1) << 3,
  NPU_WIFI_VALID_TX_DESCRIPTOR_BASE = UINT32_C(1) << 4,
  NPU_WIFI_VALID_TX_BUFFER_SPACE_BASE = UINT32_C(1) << 5,
  NPU_WIFI_VALID_TX_DONE_RING_BASE = UINT32_C(1) << 6,
  NPU_WIFI_VALID_INODE_TXRX_REGISTERS = UINT32_C(1) << 7,
  NPU_WIFI_VALID_BA_WINDOW_SIZE = UINT32_C(1) << 8,
  NPU_WIFI_VALID_DELETE_STATION = UINT32_C(1) << 9,
  NPU_WIFI_VALID_BAR_INFORMATION = UINT32_C(1) << 10,
  NPU_WIFI_VALID_INODE_DEBUG_FLAG = UINT32_C(1) << 11,
  NPU_WIFI_VALID_INODE_HARDWARE_CONFIG = UINT32_C(1) << 12,
  NPU_WIFI_VALID_TRANSFER_TO_CPU = UINT32_C(1) << 13,
  NPU_WIFI_VALID_PCIE_STATE = UINT32_C(1) << 14,
  NPU_WIFI_VALID_INODE_STOP_ACTION = UINT32_C(1) << 15,
  NPU_WIFI_VALID_INODE_PCIE_SWAP = UINT32_C(1) << 16,
};

struct npu_wifi_inode_registers {
  uint32_t direction;
  uint32_t input_count_address;
  uint32_t output_status_address;
  uint32_t output_count_address;
};

struct npu_wifi_inode_pending_configuration {
  uint32_t normal_table_address[NPU_WIFI_INODE_NORMAL_TABLE_GROUP_LIMIT];
  uint32_t normal_table_valid[NPU_WIFI_INODE_NORMAL_TABLE_VALID_WORD_COUNT];
  struct npu_wifi_inode_registers special_table;
  bool special_table_valid;
};

struct npu_wifi_interface_configuration {
  uint32_t pcie_address;
  uint32_t descriptor_count;
  uint32_t rx_descriptor_base;
  uint32_t tx_ring_pcie_address;
  uint32_t tx_descriptor_base;
  uint32_t tx_buffer_space_base;
  uint32_t tx_done_ring_base;
  uint32_t ba_window_size;
  uint32_t delete_station;
  uint32_t bar_information;
  uint32_t inode_debug_flag;
  uint32_t inode_pcie_swap;
  uint32_t debug_counter_address;
  struct npu_wifi_inode_registers inode_registers;
  uint32_t valid_fields;
  uint8_t transfer_to_cpu;
  uint8_t inode_endpoint_mask;
  uint8_t inode_vap_mask;
  bool pcie_active;
};

struct npu_wifi_arht_chip_information {
  uint32_t gpio[NPU_WIFI_ARHT_GPIO_COUNT];
  uint32_t reserved;
  uint32_t chip_id;
  bool valid;
};

struct npu_wifi_last_request {
  uint32_t interface;
  uint32_t operation;
  uint32_t command;
  uint32_t payload_length;
};

struct npu_wifi_support_map_binding {
  uint8_t *memory;
  size_t extent;
  uint32_t physical_address;
  bool active;
};

struct npu_wifi_interface_counter_snapshot {
  uint64_t counter[NPU_WIFI_COUNTER_FAMILY_COUNT];
  uint8_t flag[NPU_WIFI_COUNTER_FLAG_FAMILY_COUNT];
};

struct npu_wifi_counter_snapshot {
  struct npu_wifi_interface_counter_snapshot
      interface[NPU_WIFI_INTERFACE_COUNT];
};

struct npu_wifi_backend_operations {
  bool (*set_pcie_address)(void *context, uint32_t interface, uint32_t address);
  bool (*set_pcie_port_type)(void *context, uint32_t pcie_port_type);
  bool (*set_force_to_cpu)(void *context, bool force_to_cpu);
  bool (*initialize_rx_ring)(void *context, uint32_t interface,
                             uint32_t descriptor_count,
                             uint32_t *rx_descriptor_base);
  bool (*prepare_rx_descriptor_base)(void *context, uint32_t interface);
  bool (*set_tx_ring_pcie_address)(void *context, uint32_t interface,
                                   uint32_t address);
  bool (*set_tx_descriptor_base)(void *context, uint32_t interface,
                                 uint32_t address);
  bool (*set_tx_buffer_space_base)(void *context, uint32_t interface,
                                   uint32_t address);
  bool (*set_tx_done_ring_base)(void *context, uint32_t interface,
                                uint32_t address);
  bool (*set_delete_station)(void *context, uint32_t action, uint32_t value);
  bool (*set_dram_ba_node_address)(void *context, uint32_t address);
  bool (*set_inode_txrx_registers)(
      void *context, uint32_t interface,
      const struct npu_wifi_inode_registers *registers);
};

struct npu_wifi_configuration {
  struct npu_wifi_interface_configuration interface[NPU_WIFI_INTERFACE_COUNT];
  struct npu_wifi_inode_pending_configuration inode_pending;
  struct npu_wifi_arht_chip_information arht;
  struct npu_wifi_last_request last_request;
  struct npu_wifi_support_map_binding support_map;
  struct npu_wifi_counter_snapshot counter_snapshot;
  struct npu_wifi_npu_information_state npu_information;
  struct npu_wifi_compatibility_diagnostics compatibility_diagnostics;
  const struct npu_wifi_backend_operations *backend;
  void *backend_context;
  uint32_t rate_limit[NPU_WIFI_RATE_LIMIT_BAND_COUNT][NPU_WIFI_BSSID_COUNT];
  uint32_t dram_ba_node_address;
  uint32_t packet_buffer_address;
  uint32_t tx_packet_buffer_address;
  uint32_t tx_buffer_check_address;
  uint32_t token_id_size;
  uint32_t version_response;
  uint32_t last_rate[2];
  uint16_t flush_one_timeout;
  uint16_t flush_all_timeout;
  uint16_t error_retry_count;
  uint8_t driver_model;
  uint8_t pcie_port_type;
  bool npu_init_done;
  bool test_no_ba;
  bool force_to_cpu;
  bool fast_path_enabled;
  bool band0_on_cpu;
  bool hwnat_initialized;
  bool inode_stop_requested;
  bool dram_ba_node_address_valid;
  bool packet_buffer_address_valid;
  bool packet_buffer_address_out_of_range;
  bool tx_packet_buffer_address_valid;
  bool tx_packet_buffer_address_out_of_range;
  bool tx_buffer_check_address_valid;
  bool token_id_size_valid;
  bool pcie_port_type_valid;
  bool last_request_valid;
  uint32_t decoded_requests;
  uint32_t invalid_requests;
  uint32_t successful_requests;
  uint32_t rejected_requests;
};

struct npu_firmware_state {
  struct npu_wifi_configuration wifi;
  struct npu_tunnel_state tunnel;
  struct npu_tr471_state tr471;
  struct npu_ppe_state ppe;
  uint32_t outer_function_mask;
};

void npu_firmware_state_reset(struct npu_firmware_state *state);
void npu_wifi_configuration_set_backend(
    struct npu_wifi_configuration *configuration,
    const struct npu_wifi_backend_operations *backend, void *backend_context);
uint32_t npu_wifi_query_version_response(
    const struct npu_wifi_configuration *configuration);
bool npu_wifi_get_counter(const struct npu_wifi_configuration *configuration,
                          uint8_t *payload, size_t payload_length);
bool npu_wifi_get_support_map(struct npu_wifi_configuration *configuration,
                              uint8_t *payload, size_t payload_length);
bool npu_wifi_mail_initialize_rx_ring(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t descriptor_count);
bool npu_wifi_mail_set_transfer_to_cpu(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    bool transfer_to_cpu);
bool npu_wifi_mail_set_ba_window_size(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t window_size);
bool npu_wifi_mail_set_flush_one_timeout(
    struct npu_wifi_configuration *configuration, uint16_t timeout);
bool npu_wifi_mail_set_flush_all_timeout(
    struct npu_wifi_configuration *configuration, uint16_t timeout);
bool npu_wifi_mail_set_bar_information(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t information);
bool npu_wifi_mail_set_fast_path_flag(
    struct npu_wifi_configuration *configuration, bool enabled);
bool npu_wifi_mail_set_tx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t address);
bool npu_wifi_mail_set_inode_txrx_registers(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    const struct npu_wifi_inode_registers *registers);
bool npu_wifi_mail_set_inode_hardware_config(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint8_t endpoint_mask, uint8_t vap_mask);
bool npu_wifi_mail_set_inode_stop_action(
    struct npu_wifi_configuration *configuration, uint32_t interface);
bool npu_wifi_mail_set_inode_pcie_swap(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t value);
bool npu_wifi_mail_get_ring_size(
    const struct npu_wifi_configuration *configuration, uint32_t direction,
    uint32_t interface, uint32_t *ring_size);
bool npu_wifi_mail_get_dma_address(
    const struct npu_wifi_configuration *configuration, uint32_t direction,
    uint32_t interface, uint32_t *dma_address);
bool npu_wifi_mail_get_rx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t *descriptor_base);
bool npu_wifi_mail_set_packet_buffer_address(
    struct npu_wifi_configuration *configuration, uint32_t address);
bool npu_wifi_mail_set_tx_packet_buffer_address(
    struct npu_wifi_configuration *configuration, uint32_t address);
bool npu_wifi_mail_set_dram_ba_node_address(
    struct npu_wifi_configuration *configuration, uint32_t address);
bool npu_wifi_publish_rx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface,
    uint32_t physical_address);
bool npu_wifi_unpublish_rx_descriptor_base(
    struct npu_wifi_configuration *configuration, uint32_t interface);
bool npu_mailbox_dispatch(struct npu_firmware_state *state,
                          uint32_t outer_function, void *buffer, size_t length,
                          bool *request_applied);

#endif

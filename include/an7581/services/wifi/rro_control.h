/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_CONTROL_H
#define NPU_WIFI_RRO_CONTROL_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/rro_table_backend.h"

#define NPU_WIFI_RRO_ICV_ERROR_WORD_COUNT UINT32_C(0x402)
#define NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT UINT32_C(0x403)
#define NPU_WIFI_RRO_WCID_MAXIMUM UINT32_C(1026)
#define NPU_WIFI_RRO_TID_MAXIMUM UINT32_C(8)

enum npu_wifi_rro_station_bitmap_action {
  NPU_WIFI_RRO_STATION_BITMAP_SET = 0,
  NPU_WIFI_RRO_STATION_BITMAP_CLEAR,
  NPU_WIFI_RRO_STATION_BITMAP_QUERY,
  NPU_WIFI_RRO_STATION_BITMAP_CLEAR_ALL,
  NPU_WIFI_RRO_STATION_BITMAP_ACTION_COUNT,
};

enum npu_wifi_rro_inode_action {
  NPU_WIFI_RRO_INODE_INITIALIZE_NORMAL_TABLE = 0,
  NPU_WIFI_RRO_INODE_INITIALIZE_SPECIAL_TABLE,
  NPU_WIFI_RRO_INODE_START,
  NPU_WIFI_RRO_INODE_INVALIDATE_SELECTOR,
  NPU_WIFI_RRO_INODE_STOP,
  NPU_WIFI_RRO_INODE_UNSUPPORTED,
  NPU_WIFI_RRO_INODE_RESET_BUFFER_IDS,
  NPU_WIFI_RRO_INODE_RESUME,
  NPU_WIFI_RRO_INODE_ACTION_COUNT,
};

typedef enum npu_runtime_result (*npu_wifi_rro_control_map_table)(
    void *context, uint32_t physical_address, uint32_t length,
    volatile struct npu_wifi_rro_metadata_table_entry **entries);
typedef enum npu_runtime_result (*npu_wifi_rro_control_reset_buffers)(
    void *context);
typedef enum npu_runtime_result (*npu_wifi_rro_control_lifecycle_operation)(
    void *context);
typedef enum npu_runtime_result (*npu_wifi_rro_control_set_page_pool_address)(
    void *context, uint32_t address);

struct npu_wifi_rro_station_bitmap_result {
  uint32_t wcid;
  uint32_t tid;
  uint32_t word_before;
  uint32_t word_after;
};

struct npu_wifi_rro_control_config {
  struct npu_wifi_rro_table_backend *table_backend;
  volatile uint32_t *icv_error_table;
  npu_wifi_rro_control_map_table map_table;
  npu_wifi_rro_control_reset_buffers reset_buffers;
  npu_wifi_rro_control_set_page_pool_address set_page_pool_address;
  void *map_context;
  void *reset_context;
  void *page_pool_context;
  uint32_t icv_error_word_count;
};

struct npu_wifi_rro_control {
  struct npu_wifi_rro_table_backend *table_backend;
  volatile uint32_t *icv_error_table;
  npu_wifi_rro_control_map_table map_table;
  npu_wifi_rro_control_reset_buffers reset_buffers;
  npu_wifi_rro_control_lifecycle_operation prepare_stop;
  npu_wifi_rro_control_lifecycle_operation resume;
  npu_wifi_rro_control_set_page_pool_address set_page_pool_address;
  void *map_context;
  void *reset_context;
  void *lifecycle_context;
  void *page_pool_context;
  struct npu_wifi_configuration *configuration;
  volatile uint32_t ring_enabled;
  volatile uint32_t configuration_ready;
  uint32_t icv_error_word_count;
  uint32_t page_pool_address;
  uint32_t configured_normal_group_count;
  uint32_t applied_action_count;
  uint32_t rejected_action_count;
  uint32_t unsupported_action_count;
  uint32_t stop_generation;
  uint32_t reset_generation;
  uint32_t resume_generation;
  uint32_t station_bitmap_operation_count;
  uint32_t station_bitmap_rejected_count;
  uint32_t station_bitmap_query_count;
  uint32_t station_bitmap_clear_generation;
  uint32_t page_pool_configuration_count;
  uint32_t page_pool_rejected_count;
  uint32_t information_interface;
  bool rx_stopped;
  bool stop_prepared;
  bool reset_required;
  bool reset_completed;
  bool page_pool_address_valid;
};

enum npu_runtime_result npu_wifi_rro_control_initialize(
    struct npu_wifi_rro_control *control,
    const struct npu_wifi_rro_control_config *config);
enum npu_runtime_result npu_wifi_rro_control_bind_npu_information(
    struct npu_wifi_rro_control *control,
    struct npu_wifi_configuration *configuration, uint32_t interface);
enum npu_runtime_result npu_wifi_rro_control_bind_lifecycle(
    struct npu_wifi_rro_control *control,
    npu_wifi_rro_control_lifecycle_operation prepare_stop,
    npu_wifi_rro_control_lifecycle_operation resume, void *context);
enum npu_runtime_result
npu_wifi_rro_control_apply(struct npu_wifi_rro_control *control,
                           uint32_t action,
                           const struct npu_wifi_inode_registers *registers);
enum npu_runtime_result
npu_wifi_rro_control_finish_stop(struct npu_wifi_rro_control *control);
enum npu_runtime_result npu_wifi_rro_control_apply_station_bitmap(
    struct npu_wifi_rro_control *control, uint32_t action, uint32_t value,
    struct npu_wifi_rro_station_bitmap_result *result);
enum npu_runtime_result
npu_wifi_rro_control_set_page_pool(struct npu_wifi_rro_control *control,
                                   uint32_t address);

extern const struct npu_wifi_backend_operations
    npu_wifi_rro_control_backend_operations;

#endif

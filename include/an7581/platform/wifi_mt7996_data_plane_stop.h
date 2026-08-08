/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_DATA_PLANE_STOP_H
#define AN7581_WIFI_MT7996_DATA_PLANE_STOP_H

#include "an7581/platform/wifi_mt7996_completion_board_binding.h"
#include "an7581/platform/wifi_mt7996_ppe_result_board_binding.h"
#include "an7581/platform/wifi_mt7996_rro_control_board_binding.h"
#include "an7581/platform/wifi_mt7996_rx_refill_board_binding.h"
#include "an7581/platform/wifi_mt7996_tx_fast_path_board_binding.h"

enum an7581_wifi_mt7996_data_plane_component {
  AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL = UINT32_C(1) << 0,
  AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL = UINT32_C(1) << 1,
  AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION = UINT32_C(1) << 2,
  AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH = UINT32_C(1) << 3,
  AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT = UINT32_C(1) << 4,
};

#define AN7581_WIFI_MT7996_DATA_PLANE_COMPONENT_ALL                            \
  (AN7581_WIFI_MT7996_DATA_PLANE_RRO_CONTROL |                                 \
   AN7581_WIFI_MT7996_DATA_PLANE_RX_REFILL |                                   \
   AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION |                                  \
   AN7581_WIFI_MT7996_DATA_PLANE_TX_FAST_PATH |                                \
   AN7581_WIFI_MT7996_DATA_PLANE_PPE_RESULT)

enum an7581_wifi_mt7996_data_plane_lane {
  AN7581_WIFI_MT7996_DATA_PLANE_CORE1 = UINT32_C(1) << 0,
  AN7581_WIFI_MT7996_DATA_PLANE_CORE2 = UINT32_C(1) << 1,
  AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS = UINT32_C(1) << 2,
  AN7581_WIFI_MT7996_DATA_PLANE_CORE56 = UINT32_C(1) << 3,
};

#define AN7581_WIFI_MT7996_DATA_PLANE_LANE_ALL                                 \
  (AN7581_WIFI_MT7996_DATA_PLANE_CORE1 | AN7581_WIFI_MT7996_DATA_PLANE_CORE2 | \
   AN7581_WIFI_MT7996_DATA_PLANE_COMPLETION_HARTS |                            \
   AN7581_WIFI_MT7996_DATA_PLANE_CORE56)

struct an7581_wifi_mt7996_data_plane_board_operation {
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *context;
};

struct an7581_wifi_mt7996_data_plane_stop_config {
  struct an7581_wifi_mt7996_rro_control_lifecycle *rro_control;
  struct an7581_wifi_mt7996_rx_refill_lifecycle *rx_refill;
  struct an7581_wifi_mt7996_completion_lifecycle *completion;
  struct an7581_wifi_mt7996_tx_fast_path_lifecycle *tx_fast_path;
  struct an7581_wifi_mt7996_ppe_result_lifecycle *ppe_result;
  struct an7581_wifi_mt7996_completion_platform *completion_platform;
  struct an7581_wifi_mt7996_tx_fast_path_platform *tx_fast_path_platform;
  struct an7581_core2_dispatch *core2_dispatch;
  struct an7581_core56_dispatch *core56_dispatch;
  struct an7581_wifi_mt7996_completion_dispatch *completion_dispatch;
  const struct an7581_wifi_mt7996_rro_control_board_configuration
      *rro_control_board;
  const struct an7581_wifi_mt7996_rx_refill_board_configuration
      *rx_refill_board;
  const struct an7581_wifi_mt7996_completion_board_configuration
      *completion_board;
  const struct an7581_wifi_mt7996_tx_fast_path_board_configuration
      *tx_fast_path_board;
  const struct an7581_wifi_mt7996_ppe_result_board_configuration
      *ppe_result_board;
};

struct an7581_wifi_mt7996_data_plane_stop {
  struct an7581_wifi_mt7996_rro_control_lifecycle *rro_control;
  struct an7581_wifi_mt7996_rx_refill_lifecycle *rx_refill;
  struct an7581_wifi_mt7996_completion_lifecycle *completion;
  struct an7581_wifi_mt7996_tx_fast_path_lifecycle *tx_fast_path;
  struct an7581_wifi_mt7996_ppe_result_lifecycle *ppe_result;
  struct an7581_wifi_mt7996_completion_platform *completion_platform;
  struct an7581_wifi_mt7996_tx_fast_path_platform *tx_fast_path_platform;
  struct an7581_core1_dispatch *core1_dispatch;
  struct an7581_core2_dispatch *core2_dispatch;
  struct an7581_core56_dispatch *core56_dispatch;
  struct an7581_wifi_mt7996_completion_dispatch *completion_dispatch;
  struct an7581_wifi_mt7996_data_plane_board_operation operations[5];
  uint32_t activation_mask;
  uint32_t prepare_required_mask;
  uint32_t prepared_mask;
  uint32_t deactivated_mask;
  uint32_t lane_mask;
  uint32_t quiesce_requested_mask;
  uint32_t wake_completed_mask;
  uint32_t quiesced_mask;
  uint32_t unpublished_mask;
  uint32_t resumed_lane_mask;
  uint32_t restart_active_mask;
  uint32_t board_resumed_mask;
  uint32_t prepare_attempt_count[5];
  uint32_t wake_attempt_count[4];
  uint32_t board_resume_attempt_count[5];
  bool stop_started;
  bool stopped;
  bool restart_started;
  bool restarted;
  bool initialized;
};

struct an7581_wifi_mt7996_data_plane_stop_result {
  enum npu_runtime_result status;
  uint32_t activation_mask;
  uint32_t prepare_required_mask;
  uint32_t prepared_mask;
  uint32_t deactivated_mask;
  uint32_t lane_mask;
  uint32_t quiesce_requested_mask;
  uint32_t wake_completed_mask;
  uint32_t quiesced_mask;
  uint32_t unpublished_mask;
  uint32_t resumed_lane_mask;
  uint32_t restart_active_mask;
  uint32_t board_resumed_mask;
  bool waiting_for_owner;
  bool waiting_for_wake;
  bool waiting_for_workers;
  bool waiting_for_lifecycle;
  bool waiting_for_board_resume;
  bool stopped;
  bool restarted;
};

enum npu_runtime_result an7581_wifi_mt7996_data_plane_stop_initialize(
    struct an7581_wifi_mt7996_data_plane_stop *stop,
    const struct an7581_wifi_mt7996_data_plane_stop_config *config);
enum npu_runtime_result an7581_wifi_mt7996_data_plane_stop_step(
    struct an7581_wifi_mt7996_data_plane_stop *stop,
    struct an7581_wifi_mt7996_data_plane_stop_result *result);
enum npu_runtime_result an7581_wifi_mt7996_data_plane_restart_step(
    struct an7581_wifi_mt7996_data_plane_stop *stop,
    struct an7581_wifi_mt7996_data_plane_stop_result *result);
enum npu_runtime_result an7581_wifi_mt7996_data_plane_stop_reset(
    struct an7581_wifi_mt7996_data_plane_stop *stop);

#endif

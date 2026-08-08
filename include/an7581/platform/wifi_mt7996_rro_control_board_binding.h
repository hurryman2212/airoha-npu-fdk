/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_RRO_CONTROL_BOARD_BINDING_H
#define AN7581_WIFI_MT7996_RRO_CONTROL_BOARD_BINDING_H

#include "an7581/platform/board_stop.h"
#include "an7581/platform/wifi_mt7996_rro_control_lifecycle.h"

struct an7581_wifi_mt7996_rro_control_board_binding {
  const struct npu_wifi_mt7996_rro_pipeline_config *pipeline;
  const struct npu_wifi_mt7996_control_plane_config *control_plane;
  an7581_wifi_mt7996_rro_control_worker_wake wake_workers;
  void *wake_context;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  bool activation_allowed;
};

struct an7581_wifi_mt7996_rro_control_board_configuration {
  struct an7581_wifi_mt7996_rro_control_platform *platform;
  an7581_board_prepare_stop prepare_stop;
  an7581_board_resume resume;
  void *stop_context;
  bool activation_allowed;
};

enum npu_runtime_result an7581_wifi_mt7996_rro_control_board_binding_resolve(
    const struct an7581_wifi_mt7996_rro_control_board_binding *binding,
    struct an7581_core56_dispatch *dispatch,
    struct an7581_wifi_mt7996_rro_control_platform *platform,
    struct an7581_wifi_mt7996_rro_control_board_configuration *configuration);

#endif

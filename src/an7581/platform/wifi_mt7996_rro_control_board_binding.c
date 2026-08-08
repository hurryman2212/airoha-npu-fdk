/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_rro_control_board_binding.h"

enum npu_runtime_result an7581_wifi_mt7996_rro_control_board_binding_resolve(
    const struct an7581_wifi_mt7996_rro_control_board_binding *binding,
    struct an7581_core56_dispatch *dispatch,
    struct an7581_wifi_mt7996_rro_control_platform *platform,
    struct an7581_wifi_mt7996_rro_control_board_configuration *configuration) {
  struct an7581_wifi_mt7996_rro_control_platform_config platform_config;
  enum npu_runtime_result status;

  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *configuration =
      (struct an7581_wifi_mt7996_rro_control_board_configuration){0};
  if (binding == NULL || !binding->activation_allowed)
    return NPU_RUNTIME_SUCCESS;
  if (binding->pipeline == NULL || binding->control_plane == NULL ||
      binding->wake_workers == NULL || binding->prepare_stop == NULL ||
      binding->resume == NULL || dispatch == NULL || platform == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  platform_config = (struct an7581_wifi_mt7996_rro_control_platform_config){
      .pipeline = binding->pipeline,
      .control_plane = binding->control_plane,
      .dispatch = dispatch,
      .wake_workers = binding->wake_workers,
      .wake_context = binding->wake_context,
  };
  status = an7581_wifi_mt7996_rro_control_platform_initialize(platform,
                                                              &platform_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  configuration->platform = platform;
  configuration->prepare_stop = binding->prepare_stop;
  configuration->resume = binding->resume;
  configuration->stop_context = binding->stop_context;
  configuration->activation_allowed = true;
  return NPU_RUNTIME_SUCCESS;
}

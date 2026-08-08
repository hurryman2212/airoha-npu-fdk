/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_CORE56_DISPATCH_H
#define AN7581_CORE56_DISPATCH_H

#include "an7581/services/wifi/rro_runtime.h"

#define AN7581_CORE5_HART UINT32_C(5)
#define AN7581_CORE6_HART UINT32_C(6)

enum an7581_core56_dispatch_role {
  AN7581_CORE56_DISPATCH_ROLE_NONE = 0,
  AN7581_CORE56_DISPATCH_ROLE_CPU_QUEUE,
  AN7581_CORE56_DISPATCH_ROLE_INDICATION,
};

struct an7581_core56_dispatch {
  struct npu_wifi_rro_runtime *volatile runtime;
  volatile bool quiesce_requested;
  volatile bool core5_quiesced;
  volatile bool core6_quiesced;
};

struct an7581_core56_dispatch_result {
  struct npu_wifi_rro_runtime_step_result service;
  enum an7581_core56_dispatch_role role;
  enum npu_runtime_result status;
  bool waiting_for_runtime;
  bool quiesce_requested;
  bool quiesced;
  bool should_backoff;
};

enum npu_runtime_result
an7581_core56_dispatch_publish(struct an7581_core56_dispatch *dispatch,
                               struct npu_wifi_rro_runtime *runtime);
enum npu_runtime_result
an7581_core56_dispatch_request_quiesce(struct an7581_core56_dispatch *dispatch,
                                       struct npu_wifi_rro_runtime *runtime);
enum npu_runtime_result
an7581_core56_dispatch_unpublish(struct an7581_core56_dispatch *dispatch,
                                 struct npu_wifi_rro_runtime *runtime);
enum npu_runtime_result
an7581_core56_dispatch_resume(struct an7581_core56_dispatch *dispatch);
enum npu_runtime_result
an7581_core56_dispatch_step(struct an7581_core56_dispatch *dispatch,
                            uint32_t hart_id,
                            struct an7581_core56_dispatch_result *result);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_CORE2_DISPATCH_H
#define AN7581_CORE2_DISPATCH_H

#include "an7581/platform/tr471_runtime_dispatch.h"
#include "an7581/services/wifi/tx_fast_path_runtime.h"

#define AN7581_CORE2_HART UINT32_C(2)
#define AN7581_CORE2_HART_MASK (UINT32_C(1) << AN7581_CORE2_HART)

struct an7581_core2_worker_result {
  struct npu_wifi_tx_fast_path_step_result wifi_tx_fast_path;
  bool should_backoff;
};

typedef enum npu_runtime_result (*an7581_core2_worker_step)(
    void *context, struct an7581_core2_worker_result *result);

struct an7581_core2_dispatch {
  struct an7581_tr471_runtime_dispatch *tr471;
  an7581_core2_worker_step volatile worker;
  void *volatile worker_context;
  struct npu_wifi_tx_fast_path_runtime *volatile wifi_tx_fast_path;
  volatile bool wifi_tx_fast_path_published;
  volatile bool quiesce_requested;
  volatile bool quiesced;
  bool initialized;
};

struct an7581_core2_dispatch_result {
  struct an7581_tr471_runtime_dispatch_result tr471;
  struct npu_wifi_tx_fast_path_step_result wifi_tx_fast_path;
  enum npu_runtime_result wifi_tx_fast_path_status;
  enum npu_runtime_result status;
  bool tr471_polled;
  bool wifi_tx_fast_path_polled;
  bool waiting_for_worker;
  bool quiesce_requested;
  bool quiesced;
  bool should_backoff;
};

enum npu_runtime_result
an7581_core2_dispatch_initialize(struct an7581_core2_dispatch *dispatch,
                                 struct an7581_tr471_runtime_dispatch *tr471);
enum npu_runtime_result an7581_core2_dispatch_publish_wifi_tx_fast_path(
    struct an7581_core2_dispatch *dispatch,
    struct npu_wifi_tx_fast_path_runtime *wifi_tx_fast_path);
enum npu_runtime_result
an7581_core2_dispatch_request_quiesce(struct an7581_core2_dispatch *dispatch,
                                      void *worker_context);
enum npu_runtime_result
an7581_core2_dispatch_unpublish(struct an7581_core2_dispatch *dispatch,
                                void *worker_context);
enum npu_runtime_result
an7581_core2_dispatch_resume(struct an7581_core2_dispatch *dispatch);
enum npu_runtime_result
an7581_core2_dispatch_step(struct an7581_core2_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core2_dispatch_result *result);

#endif

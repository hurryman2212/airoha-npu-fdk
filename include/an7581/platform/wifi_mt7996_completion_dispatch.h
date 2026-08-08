/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_COMPLETION_DISPATCH_H
#define AN7581_WIFI_MT7996_COMPLETION_DISPATCH_H

#include "an7581/platform/wifi_mt7996_completion_pipeline.h"

#define AN7581_WIFI_MT7996_COMPLETION_PACKET_QUEUE_HART UINT32_C(3)
#define AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART UINT32_C(4)

enum an7581_wifi_mt7996_completion_dispatch_role {
  AN7581_WIFI_MT7996_COMPLETION_DISPATCH_NONE = 0,
  AN7581_WIFI_MT7996_COMPLETION_DISPATCH_PACKET_QUEUES,
  AN7581_WIFI_MT7996_COMPLETION_DISPATCH_TX_DONE,
};

struct an7581_wifi_mt7996_completion_dispatch {
  struct an7581_wifi_mt7996_completion_pipeline *volatile pipeline;
  volatile bool quiesce_requested;
  volatile bool packet_queue_quiesced;
  volatile bool tx_done_quiesced;
};

struct an7581_wifi_mt7996_completion_dispatch_result {
  union {
    struct npu_wifi_mt7996_completion_packet_queue_result packet_queues;
    struct npu_wifi_mt7996_completion_tx_done_result tx_done;
  } service;
  enum an7581_wifi_mt7996_completion_dispatch_role role;
  enum npu_runtime_result status;
  bool waiting_for_pipeline;
  bool quiesce_requested;
  bool quiesced;
  bool should_backoff;
};

enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_publish(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch,
    struct an7581_wifi_mt7996_completion_pipeline *pipeline);
enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_request_quiesce(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch,
    struct an7581_wifi_mt7996_completion_pipeline *pipeline);
enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_unpublish(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch,
    struct an7581_wifi_mt7996_completion_pipeline *pipeline);
enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_resume(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch);
enum npu_runtime_result an7581_wifi_mt7996_completion_dispatch_step(
    struct an7581_wifi_mt7996_completion_dispatch *dispatch, uint32_t hart_id,
    struct an7581_wifi_mt7996_completion_dispatch_result *result);

#endif

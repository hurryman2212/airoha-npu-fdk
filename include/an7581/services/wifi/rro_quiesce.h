/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_QUIESCE_H
#define NPU_WIFI_RRO_QUIESCE_H

#include "an7581/services/wifi/rro_cpu_queue.h"
#include "an7581/services/wifi/rro_descriptor.h"
#include "an7581/services/wifi/rro_packet.h"

typedef enum npu_runtime_result (*npu_wifi_rro_quiesce_operation)(
    void *context);

struct npu_wifi_rro_quiesce_config {
  struct npu_wifi_rro_indication_state *indication;
  struct npu_wifi_rro_descriptor_service *descriptor;
  struct npu_wifi_rro_packet_service *packet;
  struct npu_wifi_rro_cpu_queue *cpu_queue;
  npu_wifi_rro_indication_write32 write32;
  npu_wifi_rro_quiesce_operation reset;
  void *write_context;
  void *reset_context;
};

struct npu_wifi_rro_quiesce {
  struct npu_wifi_rro_indication_state *indication;
  struct npu_wifi_rro_descriptor_service *descriptor;
  struct npu_wifi_rro_packet_service *packet;
  struct npu_wifi_rro_cpu_queue *cpu_queue;
  npu_wifi_rro_indication_write32 write32;
  npu_wifi_rro_quiesce_operation reset;
  void *write_context;
  void *reset_context;
  uint32_t prepared_stop_count;
  uint32_t completed_reset_count;
  uint32_t completed_resume_count;
  uint32_t ownership_rejection_count;
  uint32_t publication_failure_count;
  uint32_t reset_failure_count;
  bool stop_prepared;
  bool reset_completed;
};

enum npu_runtime_result npu_wifi_rro_quiesce_initialize(
    struct npu_wifi_rro_quiesce *quiesce,
    const struct npu_wifi_rro_quiesce_config *config);
enum npu_runtime_result npu_wifi_rro_quiesce_prepare_stop(void *context);
enum npu_runtime_result npu_wifi_rro_quiesce_reset(void *context);
enum npu_runtime_result npu_wifi_rro_quiesce_resume(void *context);

#endif

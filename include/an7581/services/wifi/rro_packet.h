/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_RRO_PACKET_H
#define NPU_WIFI_RRO_PACKET_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/rro_fragment.h"
#include "an7581/services/wifi/rro_page.h"

#define NPU_WIFI_RRO_PACKET_ROUTE_MASK UINT32_C(0xf0000000)
#define NPU_WIFI_RRO_PACKET_ROUTE_ONE UINT32_C(0x10000000)
#define NPU_WIFI_RRO_PACKET_ROUTE_TWO UINT32_C(0x20000000)

typedef enum npu_runtime_result (*npu_wifi_rro_packet_enqueue)(
    void *context, uint16_t buffer_id, uint32_t packet_control);

struct npu_wifi_rro_packet_service {
  struct npu_wifi_rro_fragment_state fragment_state;
  struct npu_wifi_rro_fragment_operations operations;
  void *operations_context;
  volatile uint32_t *routed_record_counter;
  volatile uint32_t *packet_queue_release_counter;
  npu_wifi_rro_packet_enqueue enqueue;
  void *enqueue_context;
  struct npu_wifi_rro_metadata_record_fields last_record;
  struct npu_wifi_rro_metadata_record_fields pending_queue_record;
  struct npu_wifi_rro_fragment_result last_result;
  uint32_t last_record_index;
  uint32_t last_page_address;
  uint32_t committed_record_count;
  uint32_t routed_record_count;
  uint32_t queued_record_count;
  uint32_t route_bits;
  uint16_t last_page_slot;
  uint8_t queue_action_index;
  bool normal_queue_enabled;
  bool force_to_cpu;
  bool use_cpu_queue;
  bool queue_record_pending;
};

enum npu_runtime_result npu_wifi_rro_packet_initialize(
    struct npu_wifi_rro_packet_service *service,
    const struct npu_wifi_rro_fragment_operations *operations,
    void *operations_context, volatile uint32_t *routed_record_counter,
    volatile uint32_t *packet_queue_release_counter);
enum npu_runtime_result npu_wifi_rro_packet_configure_cpu_queue(
    struct npu_wifi_rro_packet_service *service,
    npu_wifi_rro_packet_enqueue enqueue, void *enqueue_context, bool enabled);
enum npu_runtime_result npu_wifi_rro_packet_set_force_to_cpu(
    struct npu_wifi_rro_packet_service *service, bool force_to_cpu);
enum npu_runtime_result npu_wifi_rro_packet_prepare_descriptor(
    void *context, const struct npu_wifi_rro_indication_descriptor *descriptor,
    uint32_t descriptor_index);
enum npu_runtime_result npu_wifi_rro_packet_consume(
    void *context, const struct npu_wifi_rro_metadata_record *record,
    uint32_t record_index, uint32_t page_address, uint16_t page_slot);

extern const struct npu_wifi_backend_operations
    npu_wifi_rro_packet_control_backend_operations;

#endif

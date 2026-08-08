/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TR471_RUNTIME_H
#define AN7581_TR471_RUNTIME_H

#include "an7581/runtime/status.h"
#include "an7581/services/tr471/mailbox.h"
#include "an7581/services/tr471/tdma.h"

#define NPU_TR471_TRANSMIT_BATCH_LIMIT 2U
#define NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT UINT32_C(0x80)
#define NPU_TR471_TIMER_TICK_NANOSECONDS UINT32_C(100000)
#define NPU_TR471_TIMER_TICKS_PER_TEN_MILLISECONDS UINT32_C(100)
#define NPU_TR471_TEST_HEADER_SIZE 32U
#define NPU_TR471_IPV4_TEST_HEADER_OFFSET 42U
#define NPU_TR471_IPV6_TEST_HEADER_OFFSET 62U
#define NPU_TR471_MINIMUM_PACKET_SIZE                                          \
  (NPU_TR471_IPV4_TEST_HEADER_OFFSET + NPU_TR471_TEST_HEADER_SIZE)
#define NPU_TR471_MAXIMUM_PACKET_SIZE                                          \
  (NPU_TR471_IPV6_TEST_HEADER_OFFSET + NPU_TR471_MAXIMUM_UDP_PAYLOAD_SIZE)

struct npu_tr471_transmit_batch {
  uint32_t packet_count;
  uint32_t payload_size;
  uint32_t final_payload_size;
};

struct npu_tr471_pending_transmit_batch {
  uint32_t remaining_packet_count;
  uint32_t payload_size;
  uint32_t final_payload_size;
};

struct npu_tr471_runtime_io {
  struct npu_tr471_state *state;
  struct npu_tr471_tdma *tdma;
  struct npu_tr471_pending_transmit_batch
      pending[NPU_TR471_TRANSMIT_BATCH_LIMIT];
  size_t pending_batch_count;
  size_t pending_batch_index;
  uint32_t transmitted_packet_count;
  uint32_t received_packet_count;
  uint32_t rejected_receive_count;
  bool initialized;
};

struct npu_tr471_runtime_step_result {
  enum npu_runtime_result transmit_status;
  enum npu_runtime_result receive_status;
  uint32_t transmitted_packet_count;
  uint32_t received_packet_count;
  uint32_t pending_transmit_packet_count;
};

enum npu_runtime_result npu_tr471_timer_tick(struct npu_tr471_state *state);
enum npu_runtime_result
npu_tr471_transmit_schedule_step(struct npu_tr471_state *state,
                                 uint32_t periodic_counter,
                                 struct npu_tr471_transmit_batch *batches,
                                 size_t batch_capacity, size_t *batch_count);
enum npu_runtime_result
npu_tr471_packet_build(struct npu_tr471_state *state, uint32_t payload_size,
                       const struct npu_tr471_clock *transmitted_at,
                       uint8_t *output, size_t output_extent,
                       size_t *packet_size);
enum npu_runtime_result npu_tr471_receive_packet(struct npu_tr471_state *state,
                                                 const uint8_t *packet,
                                                 size_t packet_size);
enum npu_runtime_result
npu_tr471_runtime_io_initialize(struct npu_tr471_runtime_io *runtime,
                                struct npu_tr471_state *state,
                                struct npu_tr471_tdma *tdma);
enum npu_runtime_result
npu_tr471_runtime_io_cancel_pending(struct npu_tr471_runtime_io *runtime);
enum npu_runtime_result
npu_tr471_runtime_io_step(struct npu_tr471_runtime_io *runtime,
                          uint32_t periodic_counter, uint32_t transmit_budget,
                          uint32_t receive_budget,
                          struct npu_tr471_runtime_step_result *result);

#endif

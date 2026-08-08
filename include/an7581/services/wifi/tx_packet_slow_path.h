/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_WIFI_TX_PACKET_SLOW_PATH_H
#define NPU_WIFI_TX_PACKET_SLOW_PATH_H

#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/tx_buffer_space.h"
#include "an7581/services/wifi/tx_packet_space.h"
#include "an7581/services/wifi/tx_ring.h"

#define NPU_WIFI_TX_SLOW_PATH_RECORD_COPY_SIZE UINT32_C(0x4c)
#define NPU_WIFI_TX_SLOW_PATH_STAGING_STRIDE UINT32_C(0x100)
#define NPU_WIFI_TX_SLOW_PATH_OUTPUT_CONTROL UINT32_C(0x004c4048)
#define NPU_WIFI_TX_SLOW_PATH_READY_MASK UINT32_C(0xff)
#define NPU_WIFI_TX_SLOW_PATH_READY UINT32_C(1)
#define NPU_WIFI_TX_SLOW_PATH_TOKEN_MASK UINT32_C(0xffff)
#define NPU_WIFI_TX_SLOW_PATH_TOKEN_VALIDITY_SHIFT UINT32_C(16)
#define NPU_WIFI_TX_SLOW_PATH_INVALID_TOKEN_CONTROL UINT32_C(0x0000ffff)
#define NPU_WIFI_TX_SLOW_PATH_DEVICE_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_TX_SLOW_PATH_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_TX_SLOW_PATH_CURRENT_WAIT_LIMIT UINT32_C(1000)
#define NPU_WIFI_TX_SLOW_PATH_PUBLISH_RETRY_LIMIT UINT32_C(5)
#define NPU_WIFI_TX_SLOW_PATH_RETRY_DELAY UINT32_C(10000)

typedef enum npu_runtime_result (*npu_wifi_tx_slow_path_copy)(
    void *context, uint32_t source_address, uint32_t destination_address,
    uint32_t length);
typedef void (*npu_wifi_tx_slow_path_delay)(void *context, uint32_t iterations);

struct npu_wifi_tx_slow_path_diagnostic_counters {
  volatile uint32_t *waits_or_publish_failures;
  volatile uint32_t *descriptor_publish_retries;
  volatile uint32_t *lookahead_descriptor_waits;
};

struct npu_wifi_tx_slow_path_band_config {
  volatile struct npu_wifi_tx_packet_descriptor *input_descriptors;
  volatile struct npu_wifi_tx_descriptor *output_descriptors;
  volatile struct npu_wifi_tx_ring_registers *registers;
  volatile uint8_t *staging_memory;
  size_t staging_memory_size;
  uint32_t staging_physical_base;
  uint32_t output_descriptor_count;
  struct npu_wifi_tx_slow_path_diagnostic_counters diagnostic_counters;
};

struct npu_wifi_tx_slow_path_config {
  struct npu_wifi_tx_slow_path_band_config band[NPU_WIFI_MT7996_TX_BAND_COUNT];
  npu_wifi_tx_slow_path_copy copy;
  npu_wifi_tx_slow_path_delay delay;
  void *copy_context;
  void *delay_context;
  struct npu_wifi_tx_producer_state *producer_state;
  const volatile bool *stop_requested;
  uint32_t token_id_limit;
};

struct npu_wifi_tx_slow_path {
  struct npu_wifi_tx_slow_path_band_config band[NPU_WIFI_MT7996_TX_BAND_COUNT];
  npu_wifi_tx_slow_path_copy copy;
  npu_wifi_tx_slow_path_delay delay;
  void *copy_context;
  void *delay_context;
  struct npu_wifi_tx_producer_state *producer_state;
  const volatile bool *stop_requested;
  struct npu_wifi_tx_producer_state local_producer_state;
  uint16_t input_consumer[NPU_WIFI_MT7996_TX_BAND_COUNT];
  uint32_t token_id_limit;
  uint32_t forwarded_packet_count;
  uint32_t malformed_packet_count;
  uint32_t full_count;
  uint32_t copy_failure_count;
  bool initialized;
};

struct npu_wifi_tx_slow_path_result {
  uint32_t band;
  uint16_t input_index;
  uint16_t output_index;
  uint16_t token_id;
  bool consumed;
  bool forwarded;
  bool malformed;
};

enum npu_runtime_result npu_wifi_tx_slow_path_initialize(
    struct npu_wifi_tx_slow_path *slow_path,
    const struct npu_wifi_tx_slow_path_config *config);
enum npu_runtime_result
npu_wifi_tx_slow_path_process(struct npu_wifi_tx_slow_path *slow_path,
                              uint32_t band_index,
                              struct npu_wifi_tx_slow_path_result *result);

#endif

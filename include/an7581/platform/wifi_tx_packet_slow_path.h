/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_TX_PACKET_SLOW_PATH_H
#define AN7581_WIFI_TX_PACKET_SLOW_PATH_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/tx_packet_slow_path.h"

#define AN7581_WIFI_MT7996_TX_SLOW_PATH_BAND0_INTERFACE UINT32_C(0)
#define AN7581_WIFI_MT7996_TX_SLOW_PATH_BAND2_INTERFACE UINT32_C(2)
#define AN7581_WIFI_TX_SLOW_PATH_BAND0_ACTIVATION_INDEX UINT32_C(0)
#define AN7581_WIFI_TX_SLOW_PATH_SECONDARY_ACTIVATION_INDEX UINT32_C(1)
#define AN7581_WIFI_TX_SLOW_PATH_VDMA_CHANNEL UINT32_C(1)

struct an7581_wifi_tx_slow_path_memory {
  struct npu_wifi_tx_slow_path_band_config band[NPU_WIFI_MT7996_TX_BAND_COUNT];
  uint32_t token_id_limit;
};

struct an7581_wifi_tx_slow_path_config {
  struct an7581_wifi_tx_slow_path_memory memory;
  struct npu_wifi_tx_producer_state *producer_state;
  const volatile bool *stop_requested;
  uint32_t vdma_poll_limit;
};

struct an7581_wifi_tx_slow_path_platform {
  struct npu_wifi_tx_slow_path service;
  uint32_t vdma_poll_limit;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_tx_slow_path_memory_resolve(
    uint32_t dynamic_base, const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_tx_slow_path_memory *memory);
enum npu_runtime_result an7581_wifi_tx_slow_path_platform_initialize(
    struct an7581_wifi_tx_slow_path_platform *platform,
    const struct an7581_wifi_tx_slow_path_config *config);

#endif

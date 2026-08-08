/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_TDM_TX_FORWARD_H
#define AN7581_WIFI_TDM_TX_FORWARD_H

#include "an7581/services/wifi/mailbox.h"
#include "an7581/services/wifi/region.h"
#include "an7581/services/wifi/tdm_tx_forward.h"

#define AN7581_WIFI_TDM_TX_BAND0_INTERFACE UINT32_C(0)
#define AN7581_WIFI_TDM_TX_BAND2_INTERFACE UINT32_C(2)

struct an7581_wifi_tdm_tx_forward_platform_config {
  const struct npu_wifi_configuration *configuration;
  struct npu_wifi_tx_producer_state *producer_state;
  volatile struct npu_wifi_mt7996_band0_diagnostic_counters
      *band0_diagnostic_counters;
  volatile struct npu_wifi_mt7996_band1_diagnostic_counters
      *band1_diagnostic_counters;
  uint32_t dynamic_base;
};

enum npu_runtime_result an7581_wifi_tdm_tx_forward_config_resolve(
    uint32_t dynamic_base, const struct npu_wifi_configuration *configuration,
    struct npu_wifi_tdm_tx_forward_config *config);
enum npu_runtime_result an7581_wifi_tdm_tx_forward_platform_initialize(
    struct npu_wifi_tdm_tx_forward *forwarder,
    const struct an7581_wifi_tdm_tx_forward_platform_config *config);

#endif

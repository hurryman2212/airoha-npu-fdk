/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_WIFI_MT7996_PPE_RESULT_H
#define AN7581_WIFI_MT7996_PPE_RESULT_H

#include "an7581/platform/plic.h"
#include "an7581/services/ppe/result_fifo.h"
#include "an7581/services/wifi/diagnostic_counters.h"
#include "an7581/services/wifi/mt7996_packet_control.h"

#define AN7581_WIFI_MT7996_PPE_RESULT_REGISTER_ADDRESS UINT32_C(0x1fb50fdc)
#define AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE UINT32_C(0x5f)
#define AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_PRIORITY UINT32_C(17)
#define AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_BUDGET                         \
  NPU_PPE_RESULT_FIFO_BATCH_LIMIT

struct an7581_wifi_mt7996_ppe_result_registers {
  uint32_t metadata;
  uint32_t status;
  uint32_t count;
};

struct an7581_wifi_mt7996_ppe_result_config {
  volatile struct an7581_wifi_mt7996_ppe_result_registers *registers;
  struct npu_wifi_mt7996_packet_control *packet_control;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
};

struct an7581_wifi_mt7996_ppe_result_statistics {
  uint32_t release_only;
  uint32_t dispatch_attempts;
  uint32_t dispatch_failures;
  uint32_t release_failures;
  uint32_t acknowledgements;
  uint32_t invalid_status_stops;
  uint32_t interrupt_count;
  uint32_t interrupt_failures;
  uint32_t unexpected_source_count;
  enum npu_runtime_result last_dispatch_status;
  enum npu_runtime_result last_release_status;
  enum npu_runtime_result last_interrupt_status;
};

struct an7581_wifi_mt7996_ppe_result_platform {
  volatile struct an7581_wifi_mt7996_ppe_result_registers *registers;
  struct npu_wifi_mt7996_packet_control *packet_control;
  volatile struct npu_wifi_mt7996_band2_diagnostic_counters
      *diagnostic_counters;
  struct an7581_wifi_mt7996_ppe_result_statistics statistics;
  bool interrupt_registered;
  bool initialized;
};

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_registers_resolve(
    volatile struct an7581_wifi_mt7996_ppe_result_registers **registers);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_initialize(
    struct an7581_wifi_mt7996_ppe_result_platform *platform,
    const struct an7581_wifi_mt7996_ppe_result_config *config);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_process(
    struct an7581_wifi_mt7996_ppe_result_platform *platform, uint32_t budget,
    struct npu_ppe_result_fifo_result *result);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_handle_interrupt(
    struct an7581_wifi_mt7996_ppe_result_platform *platform, uint32_t source);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_interrupt_register(
    struct an7581_wifi_mt7996_ppe_result_platform *platform,
    bool activation_allowed);
enum npu_runtime_result an7581_wifi_mt7996_ppe_result_interrupt_unregister(
    struct an7581_wifi_mt7996_ppe_result_platform *platform);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_ppe_result.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(offsetof(struct an7581_wifi_mt7996_ppe_result_registers,
                        metadata) == 0U,
               "MT7996 PPE result metadata offset changed");
_Static_assert(offsetof(struct an7581_wifi_mt7996_ppe_result_registers,
                        status) == 4U,
               "MT7996 PPE result status offset changed");
_Static_assert(offsetof(struct an7581_wifi_mt7996_ppe_result_registers,
                        count) == 8U,
               "MT7996 PPE result count offset changed");
_Static_assert(sizeof(struct an7581_wifi_mt7996_ppe_result_registers) == 12U,
               "MT7996 PPE result register layout changed");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static uint32_t read_count(void *context) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  return platform->registers->count;
}

static uint32_t read_status(void *context) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  return platform->registers->status;
}

static uint32_t read_metadata(void *context) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  return platform->registers->metadata;
}

static bool dispatch_packet(void *context, uint16_t buffer_id,
                            uint16_t flow_entry, uint8_t route) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  platform->statistics.last_dispatch_status =
      npu_wifi_mt7996_packet_control_enqueue(
          platform->packet_control, (int16_t)buffer_id, flow_entry, route);
  return platform->statistics.last_dispatch_status == NPU_RUNTIME_SUCCESS;
}

static void release_buffer(void *context, uint16_t buffer_id) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  platform->statistics.last_release_status =
      npu_wifi_mt7996_packet_control_release(platform->packet_control,
                                             buffer_id);
  if (platform->statistics.last_release_status != NPU_RUNTIME_SUCCESS)
    ++platform->statistics.release_failures;
}

static void acknowledge(void *context, uint32_t value) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  an7581_dma_memory_barrier();
  platform->registers->status = value;
  an7581_dma_memory_barrier();
}

static const struct npu_ppe_result_fifo_operations result_operations = {
    .read_count = read_count,
    .read_status = read_status,
    .read_metadata = read_metadata,
    .dispatch_packet = dispatch_packet,
    .release_buffer = release_buffer,
    .acknowledge = acknowledge,
};

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_registers_resolve(
    volatile struct an7581_wifi_mt7996_ppe_result_registers **registers) {
  if (registers == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  *registers =
      (volatile struct an7581_wifi_mt7996_ppe_result_registers *)(uintptr_t)
          AN7581_WIFI_MT7996_PPE_RESULT_REGISTER_ADDRESS;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_initialize(
    struct an7581_wifi_mt7996_ppe_result_platform *platform,
    const struct an7581_wifi_mt7996_ppe_result_config *config) {
  if (platform == NULL || config == NULL || config->registers == NULL ||
      config->packet_control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!pointer_is_aligned(config->registers, sizeof(uint32_t)) ||
      !config->packet_control->initialized ||
      (config->diagnostic_counters != NULL &&
       !pointer_is_aligned(config->diagnostic_counters, sizeof(uint32_t))))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(platform, 0U, sizeof(*platform));
  platform->registers = config->registers;
  platform->packet_control = config->packet_control;
  platform->diagnostic_counters = config->diagnostic_counters;
  platform->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_process(
    struct an7581_wifi_mt7996_ppe_result_platform *platform, uint32_t budget,
    struct npu_ppe_result_fifo_result *result) {
  if (platform == NULL || budget == 0U || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!npu_ppe_result_fifo_process(budget, &result_operations, platform,
                                   result))
    return NPU_RUNTIME_IO_ERROR;

  platform->statistics.release_only += result->release_only;
  platform->statistics.dispatch_attempts +=
      result->dispatched + result->dispatch_failures;
  platform->statistics.dispatch_failures += result->dispatch_failures;
  platform->statistics.acknowledgements += result->processed;
  if (platform->diagnostic_counters != NULL) {
    platform->diagnostic_counters->completion_release_only_records +=
        result->release_only;
    platform->diagnostic_counters->completion_dispatch_attempts +=
        result->dispatched + result->dispatch_failures;
    platform->diagnostic_counters->completion_dispatch_failure_releases +=
        result->dispatch_failures;
  }
  if (result->stopped_on_invalid_status)
    ++platform->statistics.invalid_status_stops;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_handle_interrupt(
    struct an7581_wifi_mt7996_ppe_result_platform *platform, uint32_t source) {
  struct npu_ppe_result_fifo_result result;

  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (source != AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE) {
    ++platform->statistics.unexpected_source_count;
    return NPU_RUNTIME_REJECTED;
  }

  platform->statistics.last_interrupt_status =
      an7581_wifi_mt7996_ppe_result_process(
          platform, AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_BUDGET, &result);
  if (platform->statistics.last_interrupt_status != NPU_RUNTIME_SUCCESS) {
    ++platform->statistics.interrupt_failures;
    return platform->statistics.last_interrupt_status;
  }

  ++platform->statistics.interrupt_count;
  return NPU_RUNTIME_SUCCESS;
}

static void result_interrupt_handler(uint32_t source, void *context) {
  struct an7581_wifi_mt7996_ppe_result_platform *platform = context;

  (void)an7581_wifi_mt7996_ppe_result_handle_interrupt(platform, source);
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_interrupt_register(
    struct an7581_wifi_mt7996_ppe_result_platform *platform,
    bool activation_allowed) {
  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!activation_allowed || platform->interrupt_registered)
    return NPU_RUNTIME_REJECTED;

  if (!an7581_plic_register_handler(
          AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE,
          result_interrupt_handler, platform))
    return NPU_RUNTIME_REJECTED;
  if (!an7581_plic_set_priority(
          AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE,
          AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_PRIORITY) ||
      !an7581_plic_set_enabled(AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE,
                               true)) {
    (void)an7581_plic_set_enabled(
        AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE, false);
    (void)an7581_plic_unregister_handler(
        AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE,
        result_interrupt_handler, platform);
    return NPU_RUNTIME_IO_ERROR;
  }

  platform->interrupt_registered = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_wifi_mt7996_ppe_result_interrupt_unregister(
    struct an7581_wifi_mt7996_ppe_result_platform *platform) {
  if (platform == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!platform->interrupt_registered)
    return NPU_RUNTIME_REJECTED;

  if (!an7581_plic_set_enabled(AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE,
                               false) ||
      !an7581_plic_unregister_handler(
          AN7581_WIFI_MT7996_PPE_RESULT_INTERRUPT_SOURCE,
          result_interrupt_handler, platform))
    return NPU_RUNTIME_IO_ERROR;

  platform->interrupt_registered = false;
  return NPU_RUNTIME_SUCCESS;
}

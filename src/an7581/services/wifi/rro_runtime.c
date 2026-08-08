/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_runtime.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool cpu_queue_operations_valid(
    const struct npu_wifi_rro_cpu_queue_operations *operations) {
  return operations != NULL && operations->dispatch != NULL &&
         operations->dispatch_special != NULL && operations->release != NULL;
}

static bool readiness_pointer_valid(const volatile uint32_t *flag) {
  return flag != NULL && ((uintptr_t)flag & (sizeof(uint32_t) - 1U)) == 0U;
}

static bool optional_counter_pointer_valid(const volatile uint32_t *counter) {
  return counter == NULL ||
         ((uintptr_t)counter & (sizeof(uint32_t) - 1U)) == 0U;
}

bool npu_wifi_rro_runtime_is_configured(
    const struct npu_wifi_rro_runtime *runtime) {
  return runtime != NULL && runtime->cpu_queue != NULL &&
         runtime->cpu_queue->entries != NULL && runtime->indication != NULL &&
         runtime->cpu_queue->entry_count != 0U &&
         (uint32_t)runtime->cpu_queue->entry_count <=
             NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT &&
         runtime->indication->descriptors != NULL &&
         runtime->descriptor != NULL &&
         readiness_pointer_valid(runtime->ring_enabled) &&
         readiness_pointer_valid(runtime->configuration_ready) &&
         optional_counter_pointer_valid(runtime->indication_attempt_counter) &&
         cpu_queue_operations_valid(&runtime->cpu_queue_operations) &&
         runtime->write32 != NULL && runtime->cpu_queue_budget != 0U &&
         runtime->indication_budget != 0U;
}

static bool runtime_is_ready(const struct npu_wifi_rro_runtime *runtime,
                             struct npu_wifi_rro_runtime_step_result *result) {
  uint32_t ring_enabled;
  uint32_t configuration_ready;

  an7581_dma_memory_barrier();
  ring_enabled = *runtime->ring_enabled;
  configuration_ready = *runtime->configuration_ready;
  an7581_dma_memory_barrier();

  result->waiting_for_ring = ring_enabled == 0U;
  result->waiting_for_configuration = configuration_ready == 0U;
  if (result->waiting_for_ring || result->waiting_for_configuration) {
    result->idle = true;
    result->should_backoff = true;
    return false;
  }
  return true;
}

enum npu_runtime_result npu_wifi_rro_runtime_initialize(
    struct npu_wifi_rro_runtime *runtime,
    const struct npu_wifi_rro_runtime_config *config) {
  if (runtime == NULL || config == NULL || config->cpu_queue == NULL ||
      config->indication == NULL || config->descriptor == NULL ||
      !cpu_queue_operations_valid(config->cpu_queue_operations) ||
      config->write32 == NULL || config->cpu_queue_budget == 0U ||
      config->indication_budget == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->cpu_queue->entries == NULL ||
      config->indication->descriptors == NULL ||
      !readiness_pointer_valid(config->ring_enabled) ||
      !readiness_pointer_valid(config->configuration_ready) ||
      !optional_counter_pointer_valid(config->indication_attempt_counter))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(runtime, 0U, sizeof(*runtime));
  runtime->cpu_queue = config->cpu_queue;
  runtime->indication = config->indication;
  runtime->descriptor = config->descriptor;
  runtime->cpu_queue_operations = *config->cpu_queue_operations;
  runtime->ring_enabled = config->ring_enabled;
  runtime->configuration_ready = config->configuration_ready;
  runtime->indication_attempt_counter = config->indication_attempt_counter;
  runtime->cpu_queue_context = config->cpu_queue_context;
  runtime->write32 = config->write32;
  runtime->write_context = config->write_context;
  runtime->cpu_queue_budget = config->cpu_queue_budget;
  runtime->indication_budget = config->indication_budget;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_runtime_step_cpu_queue(
    struct npu_wifi_rro_runtime *runtime,
    struct npu_wifi_rro_runtime_step_result *result) {
  struct npu_wifi_rro_cpu_queue_result queue_result;
  enum npu_runtime_result status;

  if (result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!npu_wifi_rro_runtime_is_configured(runtime))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!runtime_is_ready(runtime, result))
    return NPU_RUNTIME_EMPTY;

  (void)npu_memset(&queue_result, 0U, sizeof(queue_result));
  status = npu_wifi_rro_cpu_queue_consume(
      runtime->cpu_queue, runtime->cpu_queue_budget,
      &runtime->cpu_queue_operations, runtime->cpu_queue_context,
      &queue_result);
  result->completed_count = queue_result.consumed_count;
  result->pending_work = runtime->cpu_queue->pending_valid;
  result->idle = queue_result.empty && !result->pending_work;
  result->should_backoff = status != NPU_RUNTIME_SUCCESS || result->idle;
  return status;
}

enum npu_runtime_result npu_wifi_rro_runtime_step_indication(
    struct npu_wifi_rro_runtime *runtime,
    struct npu_wifi_rro_runtime_step_result *result) {
  struct npu_wifi_rro_indication_result indication_result;
  enum npu_runtime_result status;

  if (result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!npu_wifi_rro_runtime_is_configured(runtime))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!runtime_is_ready(runtime, result))
    return NPU_RUNTIME_EMPTY;

  if (runtime->indication_attempt_counter != NULL)
    ++*runtime->indication_attempt_counter;

  (void)npu_memset(&indication_result, 0U, sizeof(indication_result));
  status = npu_wifi_rro_indication_process(
      runtime->indication, runtime->indication_budget,
      npu_wifi_rro_descriptor_consume, runtime->descriptor, runtime->write32,
      runtime->write_context, &indication_result);
  result->completed_count = indication_result.consumed_count;
  result->pending_work = runtime->descriptor->active ||
                         runtime->descriptor->cursor_publication_pending;
  result->idle = indication_result.unavailable && !result->pending_work;
  if (status == NPU_RUNTIME_EMPTY)
    result->should_backoff = !result->pending_work;
  else
    result->should_backoff = status != NPU_RUNTIME_SUCCESS || result->idle;
  return status;
}

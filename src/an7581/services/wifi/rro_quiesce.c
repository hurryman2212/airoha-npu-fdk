/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_quiesce.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

static bool
descriptor_is_idle(const struct npu_wifi_rro_descriptor_service *descriptor) {
  const struct npu_wifi_rro_item_state *item = &descriptor->item_state;

  return !descriptor->active && !descriptor->descriptor_prepare_pending &&
         !descriptor->cursor_publication_pending &&
         item->phase == NPU_WIFI_RRO_ITEM_IDLE &&
         item->table_state.phase == NPU_WIFI_RRO_TABLE_IDLE &&
         !item->page_state.pending_release;
}

static bool packet_is_idle(const struct npu_wifi_rro_packet_service *packet) {
  return !packet->fragment_state.active && !packet->fragment_state.discarding &&
         !packet->fragment_state.record_pending &&
         !packet->queue_record_pending;
}

static bool cpu_queue_is_idle(const struct npu_wifi_rro_cpu_queue *queue) {
  uint32_t type;

  if (queue->entries == NULL || queue->entry_count == 0U ||
      (uint32_t)queue->entry_count > NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT ||
      queue->consumer >= queue->entry_count)
    return false;
  an7581_dma_memory_barrier();
  type = queue->entries[queue->consumer].type;
  an7581_dma_memory_barrier();
  return !queue->pending_valid && type == NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER;
}

static bool quiesce_state_is_valid(const struct npu_wifi_rro_quiesce *quiesce) {
  return quiesce != NULL && quiesce->indication != NULL &&
         quiesce->indication->descriptors != NULL &&
         quiesce->descriptor != NULL && quiesce->packet != NULL &&
         quiesce->cpu_queue != NULL && quiesce->write32 != NULL &&
         quiesce->reset != NULL;
}

static bool pipeline_is_idle(const struct npu_wifi_rro_quiesce *quiesce) {
  return descriptor_is_idle(quiesce->descriptor) &&
         packet_is_idle(quiesce->packet) &&
         cpu_queue_is_idle(quiesce->cpu_queue);
}

static enum npu_runtime_result
flush_cpu_index(struct npu_wifi_rro_quiesce *quiesce) {
  enum npu_runtime_result status = npu_wifi_rro_indication_flush_cpu_index(
      quiesce->indication, quiesce->write32, quiesce->write_context);

  if (status != NPU_RUNTIME_SUCCESS)
    ++quiesce->publication_failure_count;
  return status;
}

enum npu_runtime_result npu_wifi_rro_quiesce_initialize(
    struct npu_wifi_rro_quiesce *quiesce,
    const struct npu_wifi_rro_quiesce_config *config) {
  if (quiesce == NULL || config == NULL || config->indication == NULL ||
      config->descriptor == NULL || config->packet == NULL ||
      config->cpu_queue == NULL || config->write32 == NULL ||
      config->reset == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (config->indication->descriptors == NULL ||
      config->cpu_queue->entries == NULL ||
      config->cpu_queue->entry_count == 0U ||
      (uint32_t)config->cpu_queue->entry_count >
          NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT ||
      config->cpu_queue->consumer >= config->cpu_queue->entry_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(quiesce, 0U, sizeof(*quiesce));
  quiesce->indication = config->indication;
  quiesce->descriptor = config->descriptor;
  quiesce->packet = config->packet;
  quiesce->cpu_queue = config->cpu_queue;
  quiesce->write32 = config->write32;
  quiesce->reset = config->reset;
  quiesce->write_context = config->write_context;
  quiesce->reset_context = config->reset_context;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_quiesce_prepare_stop(void *context) {
  struct npu_wifi_rro_quiesce *quiesce = context;
  enum npu_runtime_result status;

  if (quiesce == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!quiesce_state_is_valid(quiesce))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!pipeline_is_idle(quiesce)) {
    ++quiesce->ownership_rejection_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  status = flush_cpu_index(quiesce);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  quiesce->stop_prepared = true;
  quiesce->reset_completed = false;
  ++quiesce->prepared_stop_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_quiesce_reset(void *context) {
  struct npu_wifi_rro_quiesce *quiesce = context;
  enum npu_runtime_result status;

  if (quiesce == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!quiesce_state_is_valid(quiesce))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!quiesce->stop_prepared || quiesce->reset_completed ||
      !pipeline_is_idle(quiesce)) {
    ++quiesce->ownership_rejection_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  status = flush_cpu_index(quiesce);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = quiesce->reset(quiesce->reset_context);
  if (status != NPU_RUNTIME_SUCCESS) {
    ++quiesce->reset_failure_count;
    return status;
  }

  quiesce->reset_completed = true;
  ++quiesce->completed_reset_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_quiesce_resume(void *context) {
  struct npu_wifi_rro_quiesce *quiesce = context;

  if (quiesce == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!quiesce_state_is_valid(quiesce))
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!quiesce->stop_prepared || !quiesce->reset_completed ||
      !pipeline_is_idle(quiesce)) {
    ++quiesce->ownership_rejection_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  quiesce->stop_prepared = false;
  quiesce->reset_completed = false;
  ++quiesce->completed_resume_count;
  return NPU_RUNTIME_SUCCESS;
}

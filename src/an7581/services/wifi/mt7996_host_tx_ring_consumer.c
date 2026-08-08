/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_host_tx_ring_consumer.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct npu_wifi_mt7996_host_tx_descriptor) ==
                   NPU_WIFI_MT7996_HOST_TX_DESCRIPTOR_SIZE,
               "MT7996 host-TX descriptor layout changed");
_Static_assert(offsetof(struct npu_wifi_mt7996_host_tx_descriptor, txwi) ==
                   NPU_WIFI_MT7996_HOST_TX_TXWI_OFFSET,
               "MT7996 host-TX TXWI offset changed");

static bool pointer_is_word_aligned(const volatile void *pointer) {
  return pointer != NULL &&
         ((uintptr_t)pointer & (sizeof(uint32_t) - 1U)) == 0U;
}

static bool configuration_is_valid(
    const struct npu_wifi_mt7996_host_tx_ring_consumer_config *config) {
  uint32_t band;

  if (config == NULL || config->map_ring == NULL ||
      config->transfer_txwi == NULL ||
      config->destination_descriptor_count !=
          NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT)
    return false;

  for (band = 0U; band < NPU_WIFI_MT7996_HOST_TX_BAND_COUNT; ++band) {
    if (!pointer_is_word_aligned(config->band[band].host_ring) ||
        !pointer_is_word_aligned(config->band[band].destination_descriptors))
      return false;
  }
  return true;
}

enum npu_runtime_result npu_wifi_mt7996_host_tx_ring_consumer_initialize(
    struct npu_wifi_mt7996_host_tx_ring_consumer *consumer,
    const struct npu_wifi_mt7996_host_tx_ring_consumer_config *config) {
  if (consumer == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (consumer->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(consumer, 0U, sizeof(*consumer));
  consumer->band[0] = config->band[0];
  consumer->band[1] = config->band[1];
  consumer->map_ring = config->map_ring;
  consumer->transfer_txwi = config->transfer_txwi;
  consumer->operation_context = config->operation_context;
  consumer->destination_descriptor_count = config->destination_descriptor_count;
  consumer->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint16_t next_index(uint16_t index, uint32_t count) {
  ++index;
  if ((uint32_t)index == count)
    index = 0U;
  return index;
}

static bool host_descriptor_is_ready(
    const volatile struct npu_wifi_mt7996_host_tx_descriptor *descriptor) {
  an7581_dma_memory_barrier();
  return (descriptor->control & NPU_WIFI_MT7996_HOST_TX_READY) != 0U;
}

static bool destination_is_full(
    const volatile struct npu_wifi_tx_packet_descriptor *descriptor) {
  an7581_dma_memory_barrier();
  return (descriptor->status & UINT32_C(0xff)) == NPU_WIFI_MT7996_HOST_TX_READY;
}

static enum npu_runtime_result stage_descriptor(
    struct npu_wifi_mt7996_host_tx_ring_consumer *consumer, uint32_t band,
    const volatile struct npu_wifi_mt7996_host_tx_descriptor *source) {
  volatile struct npu_wifi_tx_packet_descriptor *destination =
      &consumer->band[band]
           .destination_descriptors[consumer->destination_producer[band]];
  enum npu_runtime_result status;
  uint32_t packet_token =
      source->control >> NPU_WIFI_MT7996_HOST_TX_PACKET_TOKEN_SHIFT;
  uint32_t token_control = destination->token_control;
  uint16_t record_token;

  if (destination_is_full(destination))
    return NPU_RUNTIME_FULL;

  destination->token_control =
      (token_control & UINT32_C(0x0000ffff)) | (packet_token << 16U);
  if (packet_token != 0U) {
    destination->packet_address = source->packet_address;
    status =
        consumer->transfer_txwi(consumer->operation_context, source->txwi,
                                destination->buffer_address, &record_token);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
    destination->token_control =
        (destination->token_control & UINT32_C(0xffff0000)) | record_token;
  }

  an7581_dma_memory_barrier();
  destination->status = (destination->status & UINT32_C(0xffffff00)) |
                        NPU_WIFI_MT7996_HOST_TX_READY;
  an7581_dma_memory_barrier();
  consumer->destination_producer[band] =
      next_index(consumer->destination_producer[band],
                 consumer->destination_descriptor_count);
  return NPU_RUNTIME_SUCCESS;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

enum npu_runtime_result npu_wifi_mt7996_host_tx_ring_consume(
    struct npu_wifi_mt7996_host_tx_ring_consumer *consumer, uint32_t band,
    uint32_t budget,
    struct npu_wifi_mt7996_host_tx_ring_consumer_result *result) {
  const struct npu_wifi_mt7996_host_tx_ring_consumer_band_config *binding;
  volatile struct npu_wifi_mt7996_host_tx_descriptor *host_descriptors;
  uint32_t descriptor_count;
  uint32_t descriptor_size;
  uint32_t physical_base;
  enum npu_runtime_result status;

  if (consumer == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!consumer->initialized || band >= NPU_WIFI_MT7996_HOST_TX_BAND_COUNT ||
      budget == 0U || budget > NPU_WIFI_MT7996_HOST_TX_BATCH_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  binding = &consumer->band[band];
  an7581_dma_memory_barrier();
  physical_base = binding->host_ring->descriptor_base;
  descriptor_count = binding->host_ring->descriptor_count;
  an7581_dma_memory_barrier();
  if (physical_base == 0U || descriptor_count == 0U ||
      descriptor_count > NPU_WIFI_MT7996_HOST_TX_RING_COUNT_LIMIT ||
      consumer->host_consumer[band] >= descriptor_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  descriptor_size = descriptor_count * NPU_WIFI_MT7996_HOST_TX_DESCRIPTOR_SIZE;
  status = consumer->map_ring(consumer->operation_context, physical_base,
                              descriptor_size, &host_descriptors);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  while (result->processed < budget) {
    const volatile struct npu_wifi_mt7996_host_tx_descriptor *source =
        &host_descriptors[consumer->host_consumer[band]];

    if (!host_descriptor_is_ready(source)) {
      result->stopped_on_empty = true;
      break;
    }
    increment_counter(binding->diagnostic_counters.descriptor_attempts);
    status = stage_descriptor(consumer, band, source);
    if (status == NPU_RUNTIME_FULL) {
      ++consumer->destination_full_count;
      increment_counter(binding->diagnostic_counters.destination_full);
      result->stopped_on_destination_full = true;
      result->pending_work = true;
      break;
    }
    if (status != NPU_RUNTIME_SUCCESS) {
      ++consumer->transfer_failure_count;
      return status;
    }

    consumer->host_consumer[band] =
        next_index(consumer->host_consumer[band], descriptor_count);
    binding->host_ring->dma_index = consumer->host_consumer[band];
    an7581_dma_memory_barrier();
    ++consumer->staged_descriptor_count;
    ++result->processed;
  }

  if (result->processed == budget) {
    result->budget_exhausted = true;
    result->pending_work = true;
    increment_counter(binding->diagnostic_counters.budget_exhaustions);
  }
  result->next_host_consumer = consumer->host_consumer[band];
  result->next_destination_producer = consumer->destination_producer[band];
  if (result->stopped_on_destination_full)
    return NPU_RUNTIME_FULL;
  return result->processed == 0U ? NPU_RUNTIME_EMPTY : NPU_RUNTIME_SUCCESS;
}

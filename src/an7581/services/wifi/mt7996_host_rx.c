/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_host_rx.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

#define NPU_WIFI_MT7996_HOST_RX_OCCUPIED UINT32_C(1)
#define NPU_WIFI_MT7996_HOST_RX_TOTAL_LENGTH_SHIFT UINT32_C(15)
#define NPU_WIFI_MT7996_HOST_RX_FRAGMENT_LENGTH_SHIFT UINT32_C(1)
#define NPU_WIFI_MT7996_HOST_RX_FLAG_BIT_1_SHIFT UINT32_C(29)
#define NPU_WIFI_MT7996_HOST_RX_DELIVERY_FLAGS_SHIFT UINT32_C(26)
#define NPU_WIFI_MT7996_HOST_RX_DELIVERY_FLAGS_MASK UINT32_C(0x3f)
#define NPU_WIFI_MT7996_HOST_RX_ROUTE_SHIFT UINT32_C(16)
#define NPU_WIFI_MT7996_HOST_RX_ROUTE_MASK UINT32_C(0x1f)

_Static_assert(sizeof(struct npu_wifi_mt7996_host_rx_descriptor) ==
                   NPU_WIFI_MT7996_HOST_RX_ENTRY_SIZE,
               "MT7996 host-RX descriptor layout changed");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

static bool
memory_is_valid(volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptors,
                volatile uint32_t *producer, size_t descriptor_memory_size) {
  return descriptors != NULL && producer != NULL &&
         pointer_is_aligned(descriptors, sizeof(uint32_t)) &&
         pointer_is_aligned(producer, sizeof(uint32_t)) &&
         descriptor_memory_size >= NPU_WIFI_MT7996_HOST_RX_ENTRY_COUNT *
                                       NPU_WIFI_MT7996_HOST_RX_ENTRY_SIZE &&
         *producer < NPU_WIFI_MT7996_HOST_RX_ENTRY_COUNT;
}

enum npu_runtime_result npu_wifi_mt7996_host_rx_initialize(
    struct npu_wifi_mt7996_host_rx *host_rx,
    const struct npu_wifi_mt7996_host_rx_config *config) {
  if (host_rx == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (host_rx->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!memory_is_valid(config->descriptors, config->producer,
                       config->descriptor_memory_size) ||
      config->copy == NULL ||
      (config->diagnostic_counters.enqueue_attempts != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.enqueue_attempts,
                           sizeof(uint32_t))) ||
      (config->diagnostic_counters.ring_full != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.ring_full,
                           sizeof(uint32_t))) ||
      (config->diagnostic_counters.fallback_length_uses != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.fallback_length_uses,
                           sizeof(uint32_t))) ||
      (config->diagnostic_counters.normal_length_uses != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.normal_length_uses,
                           sizeof(uint32_t))) ||
      (config->diagnostic_counters.descriptors_built != NULL &&
       !pointer_is_aligned(config->diagnostic_counters.descriptors_built,
                           sizeof(uint32_t))))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(host_rx, 0U, sizeof(*host_rx));
  host_rx->descriptors = config->descriptors;
  host_rx->producer = config->producer;
  host_rx->copy = config->copy;
  host_rx->copy_context = config->copy_context;
  host_rx->diagnostic_counters = config->diagnostic_counters;
  host_rx->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_mt7996_host_rx_rebind_memory(
    struct npu_wifi_mt7996_host_rx *host_rx,
    volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptors,
    volatile uint32_t *producer, size_t descriptor_memory_size) {
  if (host_rx == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!host_rx->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!memory_is_valid(descriptors, producer, descriptor_memory_size))
    return NPU_RUNTIME_OUT_OF_RANGE;

  host_rx->descriptors = descriptors;
  host_rx->producer = producer;
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t advance_index(uint32_t index) {
  ++index;
  if (index == NPU_WIFI_MT7996_HOST_RX_ENTRY_COUNT)
    index = 0U;
  return index;
}

static uint32_t
copy_length(const struct npu_wifi_mt7996_packet_delivery *delivery,
            bool *oversize) {
  uint32_t length = delivery->total_length;

  *oversize = length > NPU_WIFI_MT7996_HOST_RX_COPY_LIMIT;
  if (*oversize) {
    length = delivery->fragment_length;
    if (length > NPU_WIFI_MT7996_HOST_RX_COPY_LIMIT)
      length = NPU_WIFI_MT7996_HOST_RX_COPY_LIMIT;
  }
  return length;
}

static uint32_t
descriptor_control(const struct npu_wifi_mt7996_packet_delivery *delivery) {
  return ((uint32_t)delivery->total_length
          << NPU_WIFI_MT7996_HOST_RX_TOTAL_LENGTH_SHIFT) |
         ((uint32_t)delivery->fragment_length
          << NPU_WIFI_MT7996_HOST_RX_FRAGMENT_LENGTH_SHIFT) |
         ((uint32_t)delivery->flag_bit_1
          << NPU_WIFI_MT7996_HOST_RX_FLAG_BIT_1_SHIFT) |
         NPU_WIFI_MT7996_HOST_RX_OCCUPIED;
}

static uint32_t
descriptor_metadata(const struct npu_wifi_mt7996_packet_delivery *delivery) {
  return (((uint32_t)delivery->delivery_flags &
           NPU_WIFI_MT7996_HOST_RX_DELIVERY_FLAGS_MASK)
          << NPU_WIFI_MT7996_HOST_RX_DELIVERY_FLAGS_SHIFT) |
         (((uint32_t)delivery->route & NPU_WIFI_MT7996_HOST_RX_ROUTE_MASK)
          << NPU_WIFI_MT7996_HOST_RX_ROUTE_SHIFT) |
         delivery->flow_value;
}

enum npu_runtime_result npu_wifi_mt7996_host_rx_enqueue(
    void *context, const struct npu_wifi_mt7996_packet_delivery *delivery) {
  struct npu_wifi_mt7996_host_rx *host_rx = context;
  volatile struct npu_wifi_mt7996_host_rx_descriptor *descriptor;
  volatile struct npu_wifi_mt7996_host_rx_descriptor *guard;
  enum npu_runtime_result status;
  uint32_t producer;
  uint32_t guard_index;
  uint32_t length;
  bool oversize;

  if (host_rx == NULL || delivery == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!host_rx->initialized || host_rx->descriptors == NULL ||
      host_rx->producer == NULL ||
      *host_rx->producer >= NPU_WIFI_MT7996_HOST_RX_ENTRY_COUNT ||
      delivery->source_device_address == 0U ||
      (delivery->source_device_address & (sizeof(uint32_t) - 1U)) != 0U ||
      delivery->total_length == 0U ||
      delivery->total_length > NPU_WIFI_MT7996_HOST_RX_PACKET_LENGTH_LIMIT ||
      delivery->fragment_length > NPU_WIFI_MT7996_HOST_RX_PACKET_LENGTH_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  increment_counter(host_rx->diagnostic_counters.enqueue_attempts);
  producer = *host_rx->producer;
  guard_index = advance_index(advance_index(producer));
  descriptor = &host_rx->descriptors[producer];
  guard = &host_rx->descriptors[guard_index];
  an7581_dma_memory_barrier();
  if ((guard->control & NPU_WIFI_MT7996_HOST_RX_OCCUPIED) != 0U) {
    ++host_rx->full_count;
    increment_counter(host_rx->diagnostic_counters.ring_full);
    return NPU_RUNTIME_FULL;
  }

  length = copy_length(delivery, &oversize);
  if (length == 0U || descriptor->destination_address == 0U ||
      (descriptor->destination_address & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  status = host_rx->copy(host_rx->copy_context, delivery->source_device_address,
                         descriptor->destination_address, length);
  if (status != NPU_RUNTIME_SUCCESS) {
    ++host_rx->copy_failure_count;
    return status;
  }

  descriptor->packet_word = delivery->packet_word;
  descriptor->metadata = descriptor_metadata(delivery);
  an7581_dma_memory_barrier();
  descriptor->control = descriptor_control(delivery);
  an7581_dma_memory_barrier();
  increment_counter(host_rx->diagnostic_counters.descriptors_built);
  *host_rx->producer = advance_index(producer);
  an7581_dma_memory_barrier();
  ++host_rx->enqueue_count;
  if (oversize)
    ++host_rx->oversize_length_count;
  else
    ++host_rx->normal_length_count;
  increment_counter(oversize ? host_rx->diagnostic_counters.fallback_length_uses
                             : host_rx->diagnostic_counters.normal_length_uses);
  return NPU_RUNTIME_SUCCESS;
}

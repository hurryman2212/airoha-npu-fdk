/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tdm_rx.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct an7581_qdma_descriptor) ==
                   NPU_WIFI_TDM_RX_DESCRIPTOR_SIZE,
               "TDM RX descriptor layout changed");
_Static_assert(sizeof(struct npu_wifi_tdm_rx_registers) == 16U,
               "TDM RX register layout changed");

#define NPU_WIFI_TDM_RX_PACKET_BUFFER_MINIMUM UINT32_C(0x80000000)
#define NPU_WIFI_TDM_RX_PACKET_BUFFER_LIMIT UINT32_C(0xc0000000)
#define NPU_WIFI_TDM_RX_COUNT_FIELD_MASK UINT32_C(0x00001fff)

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool
receiver_configuration_is_valid(const struct npu_wifi_tdm_rx_config *config) {
  uint32_t maximum_token_offset;
  uint32_t ring_index;

  if (config == NULL || config->global_control == NULL ||
      config->global_ring_enable == NULL || config->token_pool == NULL ||
      config->dispatch == NULL || !config->token_pool->initialized ||
      config->token_pool->token_entries == NULL ||
      config->token_pool->acquire == NULL ||
      config->token_pool->release == NULL ||
      config->token_pool->token_entry_count <=
          NPU_WIFI_TDM_RX_RING_COUNT * NPU_WIFI_TDM_RX_RING_ENTRY_COUNT ||
      config->token_pool->token_entry_count >
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT ||
      config->packet_buffer_base < NPU_WIFI_TDM_RX_PACKET_BUFFER_MINIMUM ||
      config->packet_buffer_base >= NPU_WIFI_TDM_RX_PACKET_BUFFER_LIMIT ||
      (config->packet_buffer_base & (NPU_WIFI_TDM_RX_PACKET_SIZE - 1U)) != 0U ||
      !pointer_is_aligned(config->global_control, sizeof(uint32_t)) ||
      !pointer_is_aligned(config->global_ring_enable, sizeof(uint32_t)))
    return false;

  maximum_token_offset = (config->token_pool->token_entry_count - 1U) *
                         NPU_WIFI_TDM_RX_PACKET_SIZE;
  if (config->packet_buffer_base >
      (NPU_WIFI_TDM_RX_PACKET_BUFFER_LIMIT - 1U) - maximum_token_offset)
    return false;

  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    if (config->descriptors[ring_index] == NULL ||
        config->registers[ring_index] == NULL ||
        !pointer_is_aligned(config->descriptors[ring_index],
                            sizeof(uint32_t)) ||
        !pointer_is_aligned(config->registers[ring_index], sizeof(uint32_t)) ||
        (config->diagnostic_counters[ring_index].descriptors_consumed != NULL &&
         !pointer_is_aligned(
             config->diagnostic_counters[ring_index].descriptors_consumed,
             sizeof(uint32_t))) ||
        (config->diagnostic_counters[ring_index].token_allocation_failures !=
             NULL &&
         !pointer_is_aligned(
             config->diagnostic_counters[ring_index].token_allocation_failures,
             sizeof(uint32_t))) ||
        (config->descriptor_physical_base[ring_index] &
         (NPU_WIFI_TDM_RX_DESCRIPTOR_SIZE - 1U)) != 0U)
      return false;
  }
  return true;
}

static uint32_t token_buffer_address(const struct npu_wifi_tdm_rx *receiver,
                                     uint16_t token_id) {
  return ((receiver->packet_buffer_base +
           (uint32_t)token_id * NPU_WIFI_TDM_RX_PACKET_SIZE) &
          NPU_WIFI_TDM_RX_BUFFER_ADDRESS_MASK) |
         NPU_WIFI_TDM_RX_BUFFER_DEVICE_ALIAS;
}

static bool token_from_buffer_address(const struct npu_wifi_tdm_rx *receiver,
                                      uint32_t buffer_address,
                                      uint16_t *token_id) {
  uint32_t offset;

  if (buffer_address < receiver->packet_buffer_base)
    return false;
  offset = buffer_address - receiver->packet_buffer_base;
  if ((offset & (NPU_WIFI_TDM_RX_PACKET_SIZE - 1U)) != 0U ||
      offset / NPU_WIFI_TDM_RX_PACKET_SIZE >=
          receiver->token_pool->token_entry_count)
    return false;

  *token_id = (uint16_t)(offset / NPU_WIFI_TDM_RX_PACKET_SIZE);
  return true;
}

static void release_initialized_tokens(struct npu_wifi_tdm_rx *receiver,
                                       uint32_t initialized_count) {
  uint32_t descriptor_index;
  uint32_t ring_index;
  uint16_t token_id;

  while (initialized_count != 0U) {
    --initialized_count;
    ring_index = initialized_count / NPU_WIFI_TDM_RX_RING_ENTRY_COUNT;
    descriptor_index = initialized_count % NPU_WIFI_TDM_RX_RING_ENTRY_COUNT;
    if (!token_from_buffer_address(
            receiver,
            receiver->descriptors[ring_index][descriptor_index].buffer_address,
            &token_id))
      continue;
    (void)npu_wifi_token_id_pool_release(receiver->token_pool, token_id);
  }

  receiver->initialized_descriptor_count = 0U;
}

static void configure_hardware(struct npu_wifi_tdm_rx *receiver) {
  uint32_t ring_index;

  *receiver->global_control |= NPU_WIFI_TDM_RX_GLOBAL_CONTROL_ENABLE;
  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    receiver->registers[ring_index]->descriptor_base =
        receiver->descriptor_physical_base[ring_index] &
        NPU_WIFI_TDM_RX_RING_ADDRESS_MASK;
    receiver->registers[ring_index]->descriptor_count =
        (receiver->registers[ring_index]->descriptor_count &
         ~NPU_WIFI_TDM_RX_COUNT_FIELD_MASK) |
        NPU_WIFI_TDM_RX_RING_ENTRY_COUNT;
    receiver->registers[ring_index]->cpu_index =
        NPU_WIFI_TDM_RX_RING_ENTRY_COUNT - 1U;
    receiver->registers[ring_index]->dma_index = 0U;
  }
  an7581_dma_memory_barrier();
  *receiver->global_ring_enable |= NPU_WIFI_TDM_RX_GLOBAL_RING_ENABLE;
}

enum npu_runtime_result
npu_wifi_tdm_rx_initialize(struct npu_wifi_tdm_rx *receiver,
                           const struct npu_wifi_tdm_rx_config *config) {
  volatile struct an7581_qdma_descriptor *descriptor;
  uint32_t descriptor_index;
  uint32_t ring_index;
  uint16_t token_id;
  enum npu_runtime_result status;

  if (receiver == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (receiver->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!receiver_configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(receiver, 0U, sizeof(*receiver));
  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    receiver->descriptors[ring_index] = config->descriptors[ring_index];
    receiver->registers[ring_index] = config->registers[ring_index];
    receiver->descriptor_physical_base[ring_index] =
        config->descriptor_physical_base[ring_index];
    receiver->diagnostic_counters[ring_index] =
        config->diagnostic_counters[ring_index];
  }
  receiver->global_control = config->global_control;
  receiver->global_ring_enable = config->global_ring_enable;
  receiver->token_pool = config->token_pool;
  receiver->dispatch = config->dispatch;
  receiver->publish_dispatch = config->publish_dispatch;
  receiver->dispatch_context = config->dispatch_context;
  receiver->packet_buffer_base = config->packet_buffer_base;

  for (ring_index = 0U; ring_index < NPU_WIFI_TDM_RX_RING_COUNT; ++ring_index) {
    for (descriptor_index = 0U;
         descriptor_index < NPU_WIFI_TDM_RX_RING_ENTRY_COUNT;
         ++descriptor_index) {
      status = npu_wifi_token_id_pool_allocate(receiver->token_pool, &token_id);
      if (status != NPU_RUNTIME_SUCCESS) {
        ++receiver->allocation_failure_count;
        release_initialized_tokens(receiver,
                                   receiver->initialized_descriptor_count);
        return status;
      }

      descriptor = &receiver->descriptors[ring_index][descriptor_index];
      descriptor->buffer_address = token_buffer_address(receiver, token_id);
      descriptor->control =
          (descriptor->control & NPU_WIFI_TDM_RX_DESCRIPTOR_PRESERVE_MASK) |
          NPU_WIFI_TDM_RX_PACKET_SIZE;
      ++receiver->initialized_descriptor_count;
    }
  }

  an7581_dma_memory_barrier();
  configure_hardware(receiver);
  receiver->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static void
copy_packet(const volatile struct an7581_qdma_descriptor *descriptor,
            struct npu_wifi_tdm_rx_packet *packet, uint16_t token_id) {
  uint32_t message_index;

  packet->buffer_address = descriptor->buffer_address;
  packet->token_id = token_id;
  packet->length =
      (uint16_t)(descriptor->control & NPU_WIFI_TDM_RX_DESCRIPTOR_LENGTH_MASK);
  for (message_index = 0U; message_index < 4U; ++message_index)
    packet->message[message_index] = descriptor->message[message_index];
}

static uint16_t advance_consumer(uint16_t consumer) {
  ++consumer;
  if ((uint32_t)consumer == NPU_WIFI_TDM_RX_RING_ENTRY_COUNT)
    consumer = 0U;
  return consumer;
}

static void publish_cpu_index(struct npu_wifi_tdm_rx *receiver,
                              uint32_t ring_index, uint16_t consumer) {
  uint32_t completed_index = consumer == 0U
                                 ? NPU_WIFI_TDM_RX_RING_ENTRY_COUNT - 1U
                                 : (uint32_t)consumer - 1U;

  an7581_dma_memory_barrier();
  receiver->registers[ring_index]->cpu_index = completed_index;
  ++receiver->cpu_index_publish_count;
}

enum npu_runtime_result
npu_wifi_tdm_rx_consume(struct npu_wifi_tdm_rx *receiver, uint32_t ring_index,
                        uint32_t descriptor_limit, uint32_t *processed_count) {
  volatile struct an7581_qdma_descriptor *descriptor;
  struct npu_wifi_tdm_rx_packet packet;
  enum npu_runtime_result result = NPU_RUNTIME_SUCCESS;
  enum npu_runtime_result status;
  uint32_t dispatched_since_publish = 0U;
  uint32_t processed = 0U;
  uint16_t consumer;
  uint16_t replacement_token;
  uint16_t token_id;

  if (receiver == NULL || processed_count == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  *processed_count = 0U;
  if (!receiver->initialized || ring_index >= NPU_WIFI_TDM_RX_RING_COUNT ||
      descriptor_limit == 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (descriptor_limit > NPU_WIFI_TDM_RX_BATCH_LIMIT)
    descriptor_limit = NPU_WIFI_TDM_RX_BATCH_LIMIT;

  consumer = receiver->consumer[ring_index];
  while (processed < descriptor_limit) {
    descriptor = &receiver->descriptors[ring_index][consumer];
    an7581_dma_memory_barrier();
    if ((descriptor->control & NPU_WIFI_TDM_RX_DESCRIPTOR_OWNED) == 0U)
      break;
    if (receiver->diagnostic_counters[ring_index].descriptors_consumed != NULL)
      ++*receiver->diagnostic_counters[ring_index].descriptors_consumed;
    if (!token_from_buffer_address(receiver, descriptor->buffer_address,
                                   &token_id)) {
      result = NPU_RUNTIME_OUT_OF_RANGE;
      break;
    }

    copy_packet(descriptor, &packet, token_id);
    status = npu_wifi_token_id_pool_allocate(receiver->token_pool,
                                             &replacement_token);
    if (status == NPU_RUNTIME_SUCCESS) {
      descriptor->buffer_address =
          token_buffer_address(receiver, replacement_token);
    } else {
      ++receiver->allocation_failure_count;
      if (receiver->diagnostic_counters[ring_index].token_allocation_failures !=
          NULL)
        ++*receiver->diagnostic_counters[ring_index].token_allocation_failures;
      ++receiver->dropped_packet_count;
      result = status;
    }
    an7581_dma_memory_barrier();
    descriptor->control = NPU_WIFI_TDM_RX_PACKET_SIZE;

    if (status == NPU_RUNTIME_SUCCESS) {
      status =
          receiver->dispatch(receiver->dispatch_context, ring_index, &packet);
      if (status == NPU_RUNTIME_SUCCESS) {
        ++receiver->dispatched_packet_count;
        ++dispatched_since_publish;
      } else {
        ++receiver->dispatch_failure_count;
        ++receiver->dropped_packet_count;
        result = status;
        status = npu_wifi_token_id_pool_release(receiver->token_pool, token_id);
        if (status != NPU_RUNTIME_SUCCESS)
          result = status;
      }
    }

    consumer = advance_consumer(consumer);
    ++processed;
    ++receiver->consumed_descriptor_count;
    if (processed % NPU_WIFI_TDM_RX_PUBLISH_INTERVAL == 0U)
      publish_cpu_index(receiver, ring_index, consumer);
    if (processed % NPU_WIFI_TDM_RX_PUBLISH_INTERVAL == 0U &&
        dispatched_since_publish != 0U && receiver->publish_dispatch != NULL) {
      status = receiver->publish_dispatch(receiver->dispatch_context);
      dispatched_since_publish = 0U;
      if (status != NPU_RUNTIME_SUCCESS)
        result = status;
    }
    if (result != NPU_RUNTIME_SUCCESS)
      break;
  }

  if (processed != 0U && processed % NPU_WIFI_TDM_RX_PUBLISH_INTERVAL != 0U)
    publish_cpu_index(receiver, ring_index, consumer);
  if (dispatched_since_publish != 0U && receiver->publish_dispatch != NULL) {
    status = receiver->publish_dispatch(receiver->dispatch_context);
    if (status != NPU_RUNTIME_SUCCESS && result == NPU_RUNTIME_SUCCESS)
      result = status;
  }
  receiver->consumer[ring_index] = consumer;
  *processed_count = processed;
  if (processed == 0U && result == NPU_RUNTIME_SUCCESS)
    return NPU_RUNTIME_EMPTY;
  return result;
}

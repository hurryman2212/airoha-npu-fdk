/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tr471/tdma.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct an7581_qdma_descriptor) ==
                   NPU_TR471_TDMA_DESCRIPTOR_SIZE,
               "TR-471 TDMA descriptor layout changed");
_Static_assert(sizeof(struct npu_tr471_tdma_registers) == 16U,
               "TR-471 TDMA register layout changed");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static uint16_t ring_advance(uint16_t index) {
  return (uint16_t)(((uint32_t)index + 1U) &
                    (NPU_TR471_TDMA_RING_ENTRY_COUNT - 1U));
}

static uint32_t tx_buffer_offset(uint32_t index) {
  return index * NPU_TR471_TDMA_PACKET_BUFFER_SIZE +
         NPU_TR471_TDMA_TX_PACKET_OFFSET;
}

static uint32_t rx_buffer_offset(uint32_t index) {
  return (NPU_TR471_TDMA_TX_BUFFER_COUNT + index) *
         NPU_TR471_TDMA_PACKET_BUFFER_SIZE;
}

static uint32_t device_buffer_address(uint32_t dma_base, uint32_t offset) {
  return ((dma_base + offset) & NPU_TR471_TDMA_BUFFER_ADDRESS_MASK) |
         NPU_TR471_TDMA_BUFFER_DEVICE_ALIAS;
}

static bool
common_configuration_is_valid(const struct npu_tr471_tdma_config *config) {
  return config != NULL && config->tx_descriptors != NULL &&
         config->rx_descriptors != NULL && config->tx_registers != NULL &&
         config->rx_registers != NULL && config->rx_global_control != NULL &&
         config->rx_global_ring_enable != NULL &&
         config->tx_queue_config != NULL && config->tx_queue_enable != NULL &&
         pointer_is_aligned(config->tx_descriptors, sizeof(uint32_t)) &&
         pointer_is_aligned(config->rx_descriptors, sizeof(uint32_t)) &&
         pointer_is_aligned(config->tx_registers, sizeof(uint32_t)) &&
         pointer_is_aligned(config->rx_registers, sizeof(uint32_t)) &&
         pointer_is_aligned(config->rx_global_control, sizeof(uint32_t)) &&
         pointer_is_aligned(config->rx_global_ring_enable, sizeof(uint32_t)) &&
         pointer_is_aligned(config->tx_queue_config, sizeof(uint32_t)) &&
         pointer_is_aligned(config->tx_queue_enable, sizeof(uint32_t)) &&
         (config->tx_descriptor_dma_base &
          (NPU_TR471_TDMA_DESCRIPTOR_SIZE - 1U)) == 0U &&
         (config->rx_descriptor_dma_base &
          (NPU_TR471_TDMA_DESCRIPTOR_SIZE - 1U)) == 0U;
}

static bool buffer_mapping_is_valid(const struct npu_tr471_tdma_buffer *buffer,
                                    uint32_t expected_capacity) {
  return buffer != NULL && buffer->packet != NULL &&
         (uintptr_t)buffer->packet <= UINTPTR_MAX - expected_capacity &&
         buffer->capacity == expected_capacity &&
         (buffer->device_address & ~NPU_TR471_TDMA_BUFFER_ADDRESS_MASK) ==
             NPU_TR471_TDMA_BUFFER_DEVICE_ALIAS;
}

static const struct npu_tr471_tdma_buffer *
configuration_buffer_at(const struct npu_tr471_tdma_config *config,
                        uint32_t index) {
  if (index < NPU_TR471_TDMA_TX_BUFFER_COUNT)
    return &config->tx_buffers[index];
  return &config->rx_buffers[index - NPU_TR471_TDMA_TX_BUFFER_COUNT];
}

static bool
explicit_buffer_mappings_are_valid(const struct npu_tr471_tdma_config *config) {
  const struct npu_tr471_tdma_buffer *buffer;
  const struct npu_tr471_tdma_buffer *other;
  uint32_t index;
  uint32_t other_index;

  if (config->tx_buffers == NULL || config->rx_buffers == NULL)
    return false;

  for (index = 0U; index < NPU_TR471_TDMA_PACKET_BUFFER_COUNT; ++index) {
    buffer = configuration_buffer_at(config, index);
    if (!buffer_mapping_is_valid(buffer,
                                 index < NPU_TR471_TDMA_TX_BUFFER_COUNT
                                     ? NPU_TR471_TDMA_TX_PACKET_CAPACITY
                                     : NPU_TR471_TDMA_RX_PACKET_CAPACITY))
      return false;

    for (other_index = 0U; other_index < index; ++other_index) {
      other = configuration_buffer_at(config, other_index);
      if (buffer->packet == other->packet ||
          buffer->device_address == other->device_address)
        return false;
    }
  }
  return true;
}

static bool
contiguous_buffer_mapping_is_valid(const struct npu_tr471_tdma_config *config) {
  uint32_t physical_offset;

  if (config->shared_buffers == NULL ||
      config->shared_buffer_extent <
          NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT ||
      !pointer_is_aligned(config->shared_buffers,
                          NPU_TR471_TDMA_PACKET_BUFFER_SIZE) ||
      (config->shared_buffer_dma_base &
       (NPU_TR471_TDMA_PACKET_BUFFER_SIZE - 1U)) != 0U)
    return false;

  physical_offset =
      config->shared_buffer_dma_base & NPU_TR471_TDMA_BUFFER_ADDRESS_MASK;
  return physical_offset <= (NPU_TR471_TDMA_BUFFER_ADDRESS_MASK + 1U) -
                                NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT;
}

static bool configuration_is_valid(const struct npu_tr471_tdma_config *config) {
  bool explicit_mappings;

  if (!common_configuration_is_valid(config))
    return false;

  explicit_mappings = config->tx_buffers != NULL || config->rx_buffers != NULL;
  if (explicit_mappings)
    return explicit_buffer_mappings_are_valid(config);
  return contiguous_buffer_mapping_is_valid(config);
}

static uint8_t *tx_packet(const struct npu_tr471_tdma *tdma, uint32_t index) {
  if (tdma->tx_buffers != NULL)
    return tdma->tx_buffers[index].packet;
  return tdma->shared_buffers + tx_buffer_offset(index);
}

static uint32_t tx_device_address(const struct npu_tr471_tdma *tdma,
                                  uint32_t index) {
  if (tdma->tx_buffers != NULL)
    return tdma->tx_buffers[index].device_address;
  return device_buffer_address(tdma->shared_buffer_dma_base,
                               tx_buffer_offset(index));
}

static const uint8_t *rx_packet(const struct npu_tr471_tdma *tdma,
                                uint32_t index) {
  if (tdma->rx_buffers != NULL)
    return tdma->rx_buffers[index].packet;
  return tdma->shared_buffers + rx_buffer_offset(index);
}

static uint32_t rx_device_address(const struct npu_tr471_tdma *tdma,
                                  uint32_t index) {
  if (tdma->rx_buffers != NULL)
    return tdma->rx_buffers[index].device_address;
  return device_buffer_address(tdma->shared_buffer_dma_base,
                               rx_buffer_offset(index));
}

static void initialize_tx_descriptors(struct npu_tr471_tdma *tdma) {
  volatile struct an7581_qdma_descriptor *descriptor;
  uint32_t index;

  for (index = 0U; index < NPU_TR471_TDMA_RING_ENTRY_COUNT; ++index) {
    descriptor = &tdma->tx_descriptors[index];
    descriptor->message[0] = NPU_TR471_TDMA_TX_MESSAGE0;
    descriptor->message[1] = NPU_TR471_TDMA_TX_MESSAGE1;
    descriptor->control =
        (descriptor->control | NPU_TR471_TDMA_DESCRIPTOR_OWNED) &
        ~NPU_TR471_TDMA_DESCRIPTOR_BIT30;
    descriptor->buffer_address = tx_device_address(tdma, index);
  }
}

static void initialize_rx_descriptors(struct npu_tr471_tdma *tdma) {
  volatile struct an7581_qdma_descriptor *descriptor;
  uint32_t index;

  for (index = 0U; index < NPU_TR471_TDMA_RING_ENTRY_COUNT; ++index) {
    descriptor = &tdma->rx_descriptors[index];
    descriptor->buffer_address = rx_device_address(tdma, index);
    descriptor->control =
        (descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_UPPER_PRESERVE_MASK) |
        NPU_TR471_TDMA_RX_PACKET_CAPACITY;
  }
}

static void configure_hardware(struct npu_tr471_tdma *tdma) {
  tdma->tx_registers->descriptor_base =
      tdma->tx_descriptor_dma_base & NPU_TR471_TDMA_RING_ADDRESS_MASK;
  tdma->tx_registers->descriptor_count = NPU_TR471_TDMA_RING_ENTRY_COUNT;
  tdma->tx_registers->cpu_index = 0U;
  *tdma->tx_queue_config = NPU_TR471_TDMA_TX_QUEUE_CONFIG;
  *tdma->tx_queue_enable = NPU_TR471_TDMA_TX_QUEUE_ENABLE;

  *tdma->rx_global_control |= NPU_TR471_TDMA_RX_GLOBAL_CONTROL_ENABLE;
  tdma->rx_registers->descriptor_base =
      tdma->rx_descriptor_dma_base & NPU_TR471_TDMA_RING_ADDRESS_MASK;
  tdma->rx_registers->descriptor_count =
      (tdma->rx_registers->descriptor_count & ~NPU_TR471_TDMA_RING_COUNT_MASK) |
      NPU_TR471_TDMA_RING_ENTRY_COUNT;
  tdma->rx_registers->cpu_index = NPU_TR471_TDMA_RING_ENTRY_COUNT - 1U;
  tdma->rx_registers->dma_index = 0U;
  an7581_dma_memory_barrier();
  *tdma->rx_global_ring_enable |= NPU_TR471_TDMA_RX_GLOBAL_RING_ENABLE;
}

enum npu_runtime_result
npu_tr471_tdma_initialize(struct npu_tr471_tdma *tdma,
                          const struct npu_tr471_tdma_config *config) {
  if (tdma == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (tdma->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(tdma, 0U, sizeof(*tdma));
  tdma->tx_descriptors = config->tx_descriptors;
  tdma->rx_descriptors = config->rx_descriptors;
  tdma->tx_registers = config->tx_registers;
  tdma->rx_registers = config->rx_registers;
  tdma->rx_global_control = config->rx_global_control;
  tdma->rx_global_ring_enable = config->rx_global_ring_enable;
  tdma->tx_queue_config = config->tx_queue_config;
  tdma->tx_queue_enable = config->tx_queue_enable;
  tdma->tx_buffers = config->tx_buffers;
  tdma->rx_buffers = config->rx_buffers;
  tdma->shared_buffers = config->shared_buffers;
  tdma->tx_descriptor_dma_base = config->tx_descriptor_dma_base;
  tdma->rx_descriptor_dma_base = config->rx_descriptor_dma_base;
  tdma->shared_buffer_dma_base = config->shared_buffer_dma_base;
  tdma->shared_buffer_extent = config->shared_buffer_extent;

  initialize_tx_descriptors(tdma);
  initialize_rx_descriptors(tdma);
  an7581_dma_memory_barrier();
  configure_hardware(tdma);
  tdma->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_tdma_tx_take(struct npu_tr471_tdma *tdma,
                       struct npu_tr471_tdma_tx_slot *slot) {
  volatile struct an7581_qdma_descriptor *descriptor;

  if (tdma == NULL || slot == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!tdma->initialized)
    return NPU_RUNTIME_REJECTED;

  descriptor = &tdma->tx_descriptors[tdma->tx_producer];
  an7581_dma_memory_barrier();
  if ((descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_OWNED) == 0U)
    return NPU_RUNTIME_FULL;

  slot->descriptor_index = tdma->tx_producer;
  slot->packet = tx_packet(tdma, tdma->tx_producer);
  slot->capacity = NPU_TR471_TDMA_TX_PACKET_CAPACITY;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_tdma_tx_submit(struct npu_tr471_tdma *tdma,
                         const struct npu_tr471_tdma_tx_slot *slot,
                         uint32_t packet_length, uint32_t message1) {
  volatile struct an7581_qdma_descriptor *descriptor;
  uint16_t next_producer;

  if (tdma == NULL || slot == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!tdma->initialized)
    return NPU_RUNTIME_REJECTED;
  if (slot->descriptor_index != tdma->tx_producer ||
      slot->packet != tx_packet(tdma, tdma->tx_producer) ||
      slot->capacity != NPU_TR471_TDMA_TX_PACKET_CAPACITY ||
      packet_length == 0U || packet_length > slot->capacity)
    return NPU_RUNTIME_OUT_OF_RANGE;

  descriptor = &tdma->tx_descriptors[tdma->tx_producer];
  an7581_dma_memory_barrier();
  if ((descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_OWNED) == 0U) {
    ++tdma->ownership_error_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  next_producer = ring_advance(tdma->tx_producer);
  if (tdma->tx_registers->dma_index == (uint32_t)next_producer)
    return NPU_RUNTIME_FULL;

  descriptor->control =
      (descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_UPPER_PRESERVE_MASK) |
      packet_length;
  descriptor->message[1] = message1;
  an7581_dma_memory_barrier();
  tdma->tx_producer = next_producer;
  tdma->tx_registers->cpu_index = next_producer;
  ++tdma->transmitted_packet_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_tdma_rx_take(struct npu_tr471_tdma *tdma,
                       struct npu_tr471_tdma_rx_packet *packet) {
  volatile struct an7581_qdma_descriptor *descriptor;
  uint32_t expected_address;
  uint32_t length;

  if (tdma == NULL || packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!tdma->initialized)
    return NPU_RUNTIME_REJECTED;
  if (tdma->rx_packet_outstanding)
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  descriptor = &tdma->rx_descriptors[tdma->rx_consumer];
  an7581_dma_memory_barrier();
  if ((descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_OWNED) == 0U)
    return NPU_RUNTIME_EMPTY;

  expected_address = rx_device_address(tdma, tdma->rx_consumer);
  length = descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_LENGTH_MASK;
  if (descriptor->buffer_address != expected_address || length == 0U ||
      length > NPU_TR471_TDMA_RX_PACKET_CAPACITY) {
    ++tdma->ownership_error_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  packet->packet = rx_packet(tdma, tdma->rx_consumer);
  packet->device_address = descriptor->buffer_address;
  packet->length = (uint16_t)length;
  packet->descriptor_index = tdma->rx_consumer;
  tdma->rx_packet_outstanding = true;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_tr471_tdma_rx_release(struct npu_tr471_tdma *tdma,
                          const struct npu_tr471_tdma_rx_packet *packet) {
  volatile struct an7581_qdma_descriptor *descriptor;
  uint16_t completed_index;

  if (tdma == NULL || packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!tdma->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!tdma->rx_packet_outstanding ||
      packet->descriptor_index != tdma->rx_consumer ||
      packet->packet != rx_packet(tdma, tdma->rx_consumer))
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  completed_index = tdma->rx_consumer;
  descriptor = &tdma->rx_descriptors[completed_index];
  descriptor->control =
      (descriptor->control & NPU_TR471_TDMA_DESCRIPTOR_UPPER_PRESERVE_MASK) |
      NPU_TR471_TDMA_PACKET_BUFFER_SIZE;
  an7581_dma_memory_barrier();
  tdma->rx_registers->cpu_index = completed_index;
  tdma->rx_consumer = ring_advance(tdma->rx_consumer);
  tdma->rx_packet_outstanding = false;
  ++tdma->received_packet_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_tr471_tdma_tx_reset(struct npu_tr471_tdma *tdma) {
  uint32_t index;

  if (tdma == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!tdma->initialized)
    return NPU_RUNTIME_REJECTED;

  for (index = 0U; index < NPU_TR471_TDMA_RING_ENTRY_COUNT; ++index) {
    tdma->tx_descriptors[index].message[0] = NPU_TR471_TDMA_TX_MESSAGE0;
    tdma->tx_descriptors[index].message[1] = NPU_TR471_TDMA_TX_MESSAGE1;
  }
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

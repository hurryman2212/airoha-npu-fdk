/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/mt7996_tdma_delivery.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct an7581_qdma_descriptor) ==
                   NPU_WIFI_MT7996_TDMA_DESCRIPTOR_SIZE,
               "MT7996 TDMA descriptor layout changed");
_Static_assert(sizeof(struct npu_wifi_tx_ring_registers) == 16U,
               "MT7996 TDMA register layout changed");

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool
band_config_is_valid(const struct npu_wifi_mt7996_tdma_band_config *band) {
  return band->descriptors != NULL && band->registers != NULL &&
         pointer_is_aligned(band->descriptors, sizeof(uint32_t)) &&
         pointer_is_aligned(band->registers, sizeof(uint32_t)) &&
         (band->full_observation_counter == NULL ||
          pointer_is_aligned(band->full_observation_counter,
                             sizeof(uint32_t))) &&
         (band->capacity_timeout_counter == NULL ||
          pointer_is_aligned(band->capacity_timeout_counter,
                             sizeof(uint32_t))) &&
         (band->published_counter == NULL ||
          pointer_is_aligned(band->published_counter, sizeof(uint32_t))) &&
         band->producer < NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT;
}

static bool band_is_enabled(uint32_t band_mask, uint32_t band) {
  return (band_mask & (UINT32_C(1) << band)) != 0U;
}

static bool band_configuration_is_valid(
    const struct npu_wifi_mt7996_tdma_delivery_config *config) {
  uint32_t band;

  if (config->enabled_band_mask == 0U ||
      (config->enabled_band_mask & ~NPU_WIFI_MT7996_TDMA_ALL_BANDS_MASK) != 0U)
    return false;

  for (band = 0U; band < NPU_WIFI_MT7996_TDMA_BAND_COUNT; ++band) {
    if (band_is_enabled(config->enabled_band_mask, band)) {
      if (!band_config_is_valid(&config->band[band]))
        return false;
    } else if (config->band[band].descriptors != NULL ||
               config->band[band].registers != NULL ||
               config->band[band].producer != 0U) {
      return false;
    }
  }
  return true;
}

static bool packet_dma_range_is_valid(
    const struct npu_wifi_mt7996_tdma_delivery_config *config) {
  uint32_t packet_span;

  if (config->rro_packet_count == 0U ||
      config->rro_packet_count > UINT32_C(0x10000))
    return false;

  packet_span =
      config->rro_packet_count * NPU_WIFI_MT7996_TDMA_RRO_PACKET_STRIDE;
  return config->rro_packet_dma_base <=
         NPU_WIFI_MT7996_TDMA_ADDRESS_MASK - packet_span + 1U;
}

enum npu_runtime_result npu_wifi_mt7996_tdma_delivery_initialize(
    struct npu_wifi_mt7996_tdma_delivery *delivery,
    const struct npu_wifi_mt7996_tdma_delivery_config *config) {
  struct npu_wifi_mt7996_packet_control_config packet_control_config;
  enum npu_runtime_result status;
  uint32_t band;

  if (delivery == NULL || config == NULL || config->enqueue == NULL ||
      config->release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (delivery->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!packet_dma_range_is_valid(config) ||
      !band_configuration_is_valid(config))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(delivery, 0U, sizeof(*delivery));
  packet_control_config = (struct npu_wifi_mt7996_packet_control_config){
      .packet_mapping = config->rro_packet_mapping,
      .enqueue = config->enqueue,
      .release = config->release,
      .packet_context = config->packet_context,
      .packet_mapping_size = config->rro_packet_mapping_size,
      .packet_count = config->rro_packet_count,
  };
  status = npu_wifi_mt7996_packet_control_initialize(&delivery->packet_control,
                                                     &packet_control_config);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  for (band = 0U; band < NPU_WIFI_MT7996_TDMA_BAND_COUNT; ++band) {
    if (band_is_enabled(config->enabled_band_mask, band))
      delivery->band[band] = config->band[band];
  }
  delivery->rro_packet_dma_base = config->rro_packet_dma_base;
  delivery->enabled_band_mask = config->enabled_band_mask;
  delivery->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint32_t free_descriptor_count(uint32_t producer, uint32_t dma_index) {
  if (producer < dma_index)
    return dma_index - producer - 1U;
  return NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT - 1U - producer + dma_index;
}

static void capacity_retry_delay(void) {
  uint32_t iteration;

  for (iteration = 0U;
       iteration < NPU_WIFI_MT7996_TDMA_CAPACITY_DELAY_ITERATIONS; ++iteration)
    __asm__ volatile("" ::: "memory");
}

static enum npu_runtime_result
wait_for_capacity(struct npu_wifi_mt7996_tdma_delivery *delivery,
                  uint32_t band) {
  struct npu_wifi_mt7996_tdma_band_config *band_state = &delivery->band[band];
  struct npu_wifi_mt7996_tdma_statistics *statistics =
      &delivery->statistics[band];
  uint32_t attempt;

  for (attempt = 0U; attempt < NPU_WIFI_MT7996_TDMA_CAPACITY_ATTEMPTS;
       ++attempt) {
    uint32_t dma_index;

    an7581_dma_memory_barrier();
    dma_index = band_state->registers->dma_index;
    if (dma_index >= NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT)
      return NPU_RUNTIME_OUT_OF_RANGE;
    if (free_descriptor_count(band_state->producer, dma_index) >=
        NPU_WIFI_MT7996_TDMA_MINIMUM_FREE)
      return NPU_RUNTIME_SUCCESS;
    capacity_retry_delay();
    ++statistics->full_observation_count;
    if (band_state->full_observation_counter != NULL)
      ++*band_state->full_observation_counter;
  }

  ++statistics->capacity_timeout_count;
  if (band_state->capacity_timeout_counter != NULL)
    ++*band_state->capacity_timeout_counter;
  return NPU_RUNTIME_FULL;
}

static bool
request_is_valid(const struct npu_wifi_mt7996_tdma_delivery *delivery,
                 const struct npu_wifi_mt7996_tdma_request *request) {
  if (request == NULL ||
      (uint32_t)request->band >= NPU_WIFI_MT7996_TDMA_BAND_COUNT ||
      request->packet_id > (UINT32_MAX >> NPU_WIFI_MT7996_TDMA_PACKET_ID_SHIFT))
    return false;
  if (request->length >= NPU_WIFI_MT7996_TDMA_MINIMUM_FRAME_LENGTH)
    return true;
  return request->packet_data != NULL &&
         request->packet_data_size >=
             NPU_WIFI_MT7996_TDMA_MINIMUM_FRAME_LENGTH &&
         delivery->initialized;
}

static void
zero_pad_packet(const struct npu_wifi_mt7996_tdma_request *request) {
  uint32_t offset;

  for (offset = request->length;
       offset < NPU_WIFI_MT7996_TDMA_MINIMUM_FRAME_LENGTH; ++offset)
    request->packet_data[offset] = 0U;
}

enum npu_runtime_result npu_wifi_mt7996_tdma_delivery_publish(
    struct npu_wifi_mt7996_tdma_delivery *delivery,
    const struct npu_wifi_mt7996_tdma_request *request) {
  volatile struct an7581_qdma_descriptor *descriptor;
  struct npu_wifi_mt7996_tdma_band_config *band_state;
  struct npu_wifi_mt7996_tdma_statistics *statistics;
  enum npu_runtime_result status;
  uint32_t length;
  uint32_t next_producer;

  if (delivery == NULL || request == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!delivery->initialized)
    return NPU_RUNTIME_REJECTED;
  if (!request_is_valid(delivery, request) ||
      !band_is_enabled(delivery->enabled_band_mask, request->band))
    return NPU_RUNTIME_OUT_OF_RANGE;

  band_state = &delivery->band[request->band];
  statistics = &delivery->statistics[request->band];
  status = wait_for_capacity(delivery, request->band);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  descriptor = &band_state->descriptors[band_state->producer];
  an7581_dma_memory_barrier();
  if ((descriptor->control & NPU_WIFI_TX_DESCRIPTOR_READY) == 0U) {
    ++statistics->full_observation_count;
    if (band_state->full_observation_counter != NULL)
      ++*band_state->full_observation_counter;
    return NPU_RUNTIME_FULL;
  }

  ++statistics->published_count;
  if (band_state->published_counter != NULL)
    ++*band_state->published_counter;
  length = request->length;
  if (length < NPU_WIFI_MT7996_TDMA_MINIMUM_FRAME_LENGTH) {
    zero_pad_packet(request);
    length = NPU_WIFI_MT7996_TDMA_MINIMUM_FRAME_LENGTH;
  }
  next_producer = band_state->producer + 1U;
  if (next_producer == NPU_WIFI_MT7996_TDMA_DESCRIPTOR_COUNT)
    next_producer = 0U;

  descriptor->message[0] =
      (request->packet_id << NPU_WIFI_MT7996_TDMA_PACKET_ID_SHIFT) |
      NPU_WIFI_TX_DESCRIPTOR_READY;
  descriptor->buffer_address =
      (request->packet_address & NPU_WIFI_MT7996_TDMA_ADDRESS_MASK) |
      NPU_WIFI_TX_DESCRIPTOR_READY;
  an7581_dma_memory_barrier();
  descriptor->control = length & UINT32_C(0xffff);
  an7581_dma_memory_barrier();
  band_state->registers->cpu_index = next_producer;
  band_state->producer = next_producer;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result dispatch_normal(void *context, int16_t buffer_id,
                                               uint16_t total_length,
                                               uint8_t flags,
                                               uint16_t fragment_length) {
  struct npu_wifi_mt7996_tdma_delivery *delivery = context;
  enum npu_runtime_result status;

  if (delivery == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!delivery->initialized)
    return NPU_RUNTIME_REJECTED;
  status = delivery->packet_control.enqueue(
      delivery->packet_control.packet_context, buffer_id, total_length, 0U, 0U,
      1U, flags, fragment_length);

  return status == NPU_RUNTIME_FULL ? NPU_RUNTIME_REJECTED : status;
}

static enum npu_runtime_result dispatch_special(void *context,
                                                uint16_t buffer_id,
                                                uint16_t payload_length,
                                                uint16_t data_offset) {
  struct npu_wifi_mt7996_tdma_delivery *delivery = context;
  struct npu_wifi_mt7996_tdma_request request;
  enum npu_runtime_result status;
  uint32_t packet_offset;
  uint32_t payload_offset;

  if (delivery == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!delivery->initialized)
    return NPU_RUNTIME_REJECTED;
  if ((uint32_t)buffer_id >= delivery->packet_control.packet_count ||
      data_offset > NPU_WIFI_MT7996_TDMA_RRO_PACKET_STRIDE -
                        NPU_WIFI_MT7996_TDMA_RRO_PAYLOAD_OFFSET)
    return NPU_RUNTIME_OUT_OF_RANGE;

  payload_offset = NPU_WIFI_MT7996_TDMA_RRO_PAYLOAD_OFFSET + data_offset;
  if ((uint32_t)payload_length >
      NPU_WIFI_MT7996_TDMA_RRO_PACKET_STRIDE - payload_offset)
    return NPU_RUNTIME_OUT_OF_RANGE;
  packet_offset = (uint32_t)buffer_id * NPU_WIFI_MT7996_TDMA_RRO_PACKET_STRIDE +
                  payload_offset;
  request = (struct npu_wifi_mt7996_tdma_request){
      .packet_data = delivery->packet_control.packet_mapping + packet_offset,
      .packet_data_size =
          NPU_WIFI_MT7996_TDMA_RRO_PACKET_STRIDE - payload_offset,
      .packet_id = buffer_id,
      .packet_address = delivery->rro_packet_dma_base + packet_offset,
      .length = payload_length,
      .band = 0U,
  };
  status = npu_wifi_mt7996_tdma_delivery_publish(delivery, &request);
  return status == NPU_RUNTIME_FULL ? NPU_RUNTIME_REJECTED : status;
}

static enum npu_runtime_result release_packet(void *context,
                                              uint16_t buffer_id) {
  struct npu_wifi_mt7996_tdma_delivery *delivery = context;

  if (delivery == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!delivery->initialized)
    return NPU_RUNTIME_REJECTED;
  return npu_wifi_mt7996_packet_control_release(&delivery->packet_control,
                                                buffer_id);
}

const struct npu_wifi_rro_cpu_queue_operations
    npu_wifi_mt7996_tdma_rro_operations = {
        .dispatch = dispatch_normal,
        .dispatch_special = dispatch_special,
        .release = release_packet,
};

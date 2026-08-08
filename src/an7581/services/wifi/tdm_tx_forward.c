/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/tdm_tx_forward.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

_Static_assert(sizeof(struct npu_wifi_tx_ring_registers) == 16U,
               "TDM TX register layout changed");
_Static_assert(sizeof(struct npu_wifi_tx_descriptor) ==
                   NPU_WIFI_TX_DESCRIPTOR_SIZE,
               "TDM TX descriptor layout changed");
_Static_assert(sizeof(struct npu_wifi_tx_buffer_space_record) ==
                   NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE,
               "TDM TX record layout changed");

#define NPU_WIFI_TDM_TX_RECORD_PHYSICAL_LIMIT UINT32_C(0x40000000)
#define NPU_WIFI_TDM_TX_MESSAGE_BAND_SHIFT UINT32_C(25)
#define NPU_WIFI_TDM_TX_MESSAGE_BAND_MASK UINT32_C(1)
#define NPU_WIFI_TDM_TX_MESSAGE_ROUTE_MASK UINT32_C(0x1f)
#define NPU_WIFI_TDM_TX_MESSAGE_ROUTE_SHIFT UINT32_C(1)
#define NPU_WIFI_TDM_TX_MESSAGE_WCID_SHIFT UINT32_C(14)
#define NPU_WIFI_TDM_TX_MESSAGE_WCID_MASK UINT32_C(0x7ff)
#define NPU_WIFI_TDM_TX_MESSAGE_WCID_SENTINEL UINT32_C(0x7ff)
#define NPU_WIFI_TDM_TX_MESSAGE_FLAGS_SHIFT UINT32_C(24)
#define NPU_WIFI_TDM_TX_MESSAGE_FLAGS_MASK UINT32_C(0x7f)

static bool pointer_is_aligned(const volatile void *pointer, size_t alignment) {
  return ((uintptr_t)pointer & (alignment - 1U)) == 0U;
}

static bool
band_configuration_is_valid(const struct npu_wifi_tdm_tx_band_config *band,
                            uint32_t band_index) {
  uint32_t expected_count = band_index == 0U
                                ? NPU_WIFI_TDM_TX_BAND0_DESCRIPTOR_COUNT
                                : NPU_WIFI_TDM_TX_SECONDARY_DESCRIPTOR_COUNT;
  uint32_t record_span;

  if (band->descriptors == NULL || band->records == NULL ||
      band->registers == NULL || band->descriptor_count != expected_count ||
      !pointer_is_aligned(band->descriptors, sizeof(uint32_t)) ||
      !pointer_is_aligned(band->records, sizeof(uint32_t)) ||
      !pointer_is_aligned(band->registers, sizeof(uint32_t)) ||
      (band->diagnostic_counters.current_descriptor_waits != NULL &&
       !pointer_is_aligned(band->diagnostic_counters.current_descriptor_waits,
                           sizeof(uint32_t))) ||
      (band->diagnostic_counters.descriptor_publish_retries != NULL &&
       !pointer_is_aligned(band->diagnostic_counters.descriptor_publish_retries,
                           sizeof(uint32_t))) ||
      (band->diagnostic_counters.lookahead_descriptor_waits != NULL &&
       !pointer_is_aligned(band->diagnostic_counters.lookahead_descriptor_waits,
                           sizeof(uint32_t))) ||
      (band->record_physical_base & (sizeof(uint32_t) - 1U)) != 0U)
    return false;

  record_span = band->descriptor_count * NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE;
  return band->record_physical_base < NPU_WIFI_TDM_TX_RECORD_PHYSICAL_LIMIT &&
         record_span <=
             NPU_WIFI_TDM_TX_RECORD_PHYSICAL_LIMIT - band->record_physical_base;
}

enum npu_runtime_result npu_wifi_tdm_tx_forward_initialize(
    struct npu_wifi_tdm_tx_forward *forwarder,
    const struct npu_wifi_tdm_tx_forward_config *config) {
  uint32_t band_index;

  if (forwarder == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (forwarder->initialized)
    return NPU_RUNTIME_REJECTED;
  for (band_index = 0U; band_index < NPU_WIFI_TDM_TX_BAND_COUNT; ++band_index) {
    if (!band_configuration_is_valid(&config->band[band_index], band_index))
      return NPU_RUNTIME_OUT_OF_RANGE;
  }
  if (config->producer_state != NULL &&
      !npu_wifi_tx_producer_state_is_valid(config->producer_state))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(forwarder, 0U, sizeof(*forwarder));
  for (band_index = 0U; band_index < NPU_WIFI_TDM_TX_BAND_COUNT; ++band_index)
    forwarder->band[band_index] = config->band[band_index];
  forwarder->producer_state = config->producer_state != NULL
                                  ? config->producer_state
                                  : &forwarder->local_producer_state;
  forwarder->delay = config->delay;
  forwarder->delay_context = config->delay_context;
  forwarder->stop_requested = config->stop_requested;
  forwarder->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static uint16_t advance_producer(uint16_t producer, uint32_t descriptor_count) {
  ++producer;
  if ((uint32_t)producer == descriptor_count)
    producer = 0U;
  return producer;
}

static uint32_t packet_band(const struct npu_wifi_tdm_rx_packet *packet) {
  return (packet->message[0] >> NPU_WIFI_TDM_TX_MESSAGE_BAND_SHIFT) &
         NPU_WIFI_TDM_TX_MESSAGE_BAND_MASK;
}

static bool output_descriptor_is_available(
    const volatile struct npu_wifi_tx_descriptor *descriptor) {
  an7581_dma_memory_barrier();
  return (descriptor->control & NPU_WIFI_TX_DESCRIPTOR_READY) != 0U;
}

static bool stop_requested(const struct npu_wifi_tdm_tx_forward *forwarder) {
  return forwarder->stop_requested != NULL && *forwarder->stop_requested;
}

static void increment_counter(volatile uint32_t *counter) {
  if (counter != NULL)
    ++*counter;
}

static bool wait_for_current_descriptor(
    struct npu_wifi_tdm_tx_forward *forwarder,
    const struct npu_wifi_tdm_tx_band_config *band,
    const volatile struct npu_wifi_tx_descriptor *descriptor) {
  uint32_t attempts_remaining = NPU_WIFI_TDM_TX_CURRENT_WAIT_LIMIT;

  while (!output_descriptor_is_available(descriptor) &&
         attempts_remaining != 0U && !stop_requested(forwarder)) {
    if (forwarder->delay == NULL)
      return false;
    increment_counter(band->diagnostic_counters.current_descriptor_waits);
    forwarder->delay(forwarder->delay_context, NPU_WIFI_TDM_TX_RETRY_DELAY);
    --attempts_remaining;
  }
  return output_descriptor_is_available(descriptor);
}

static bool wait_for_lookahead_descriptor(
    struct npu_wifi_tdm_tx_forward *forwarder,
    const struct npu_wifi_tdm_tx_band_config *band,
    const volatile struct npu_wifi_tx_descriptor *descriptor) {
  while (!output_descriptor_is_available(descriptor) &&
         !stop_requested(forwarder)) {
    if (forwarder->delay == NULL)
      return false;
    increment_counter(band->diagnostic_counters.lookahead_descriptor_waits);
    forwarder->delay(forwarder->delay_context, NPU_WIFI_TDM_TX_RETRY_DELAY);
  }
  return output_descriptor_is_available(descriptor);
}

static void
prepare_record(volatile struct npu_wifi_tx_buffer_space_record *record,
               const struct npu_wifi_tdm_rx_packet *packet) {
  uint32_t message0 = packet->message[0];
  uint32_t message2 = packet->message[2];
  uint32_t route = ((message0 >> NPU_WIFI_TDM_TX_MESSAGE_BAND_SHIFT) &
                    NPU_WIFI_TDM_TX_MESSAGE_ROUTE_MASK) >>
                   NPU_WIFI_TDM_TX_MESSAGE_ROUTE_SHIFT;
  uint32_t wcid = (message0 >> NPU_WIFI_TDM_TX_MESSAGE_WCID_SHIFT) &
                  NPU_WIFI_TDM_TX_MESSAGE_WCID_MASK;

  record->token_control = ((message2 >> 31) << 2) |
                          ((uint32_t)packet->token_id << 16) |
                          NPU_WIFI_TDM_TX_RECORD_TOKEN_BASE;
  record->route_control = ((message2 >> NPU_WIFI_TDM_TX_MESSAGE_FLAGS_SHIFT) &
                           NPU_WIFI_TDM_TX_MESSAGE_FLAGS_MASK) |
                          (route << 8) | NPU_WIFI_TDM_TX_RECORD_ROUTE_BASE;
  record->station_control =
      wcid == NPU_WIFI_TDM_TX_MESSAGE_WCID_SENTINEL
          ? NPU_WIFI_TDM_TX_RECORD_WCID_SENTINEL
          : (wcid << 16) | NPU_WIFI_TDM_TX_RECORD_WCID_SUFFIX;
  record->packet_address = packet->buffer_address;
  record->packet_length = (uint32_t)packet->length;
}

static bool
publish_descriptor(struct npu_wifi_tdm_tx_forward *forwarder,
                   const struct npu_wifi_tdm_tx_band_config *band,
                   volatile struct npu_wifi_tx_descriptor *descriptor,
                   uint32_t record_address) {
  uint32_t retries_remaining = NPU_WIFI_TDM_TX_PUBLISH_RETRY_LIMIT;

  for (;;) {
    descriptor->buffer0 =
        record_address & NPU_WIFI_TDM_TX_DESCRIPTOR_ADDRESS_MASK;
    descriptor->buffer1 = 0U;
    an7581_dma_memory_barrier();
    descriptor->control = NPU_WIFI_TDM_TX_DESCRIPTOR_CONTROL;
    an7581_dma_memory_barrier();
    if (!output_descriptor_is_available(descriptor))
      return true;
    if (retries_remaining == 0U)
      return false;
    if (forwarder->delay != NULL)
      forwarder->delay(forwarder->delay_context, NPU_WIFI_TDM_TX_RETRY_DELAY);
    --retries_remaining;
    increment_counter(band->diagnostic_counters.descriptor_publish_retries);
  }
}

enum npu_runtime_result
npu_wifi_tdm_tx_forward_dispatch(void *context, uint32_t ring_index,
                                 const struct npu_wifi_tdm_rx_packet *packet) {
  struct npu_wifi_tdm_tx_forward *forwarder = context;
  volatile struct npu_wifi_tx_buffer_space_record *record;
  volatile struct npu_wifi_tx_descriptor *descriptor;
  volatile struct npu_wifi_tx_descriptor *next_descriptor;
  struct npu_wifi_tdm_tx_band_config *band;
  uint32_t band_index;
  uint32_t record_address;
  uint16_t next_producer;
  uint16_t producer;

  (void)ring_index;
  if (forwarder == NULL || packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!forwarder->initialized || packet->length > NPU_WIFI_TDM_RX_PACKET_SIZE ||
      (uint32_t)packet->token_id >= NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  band_index = packet_band(packet);
  band = &forwarder->band[band_index];
  producer = forwarder->producer_state->index[band_index];
  next_producer = advance_producer(producer, band->descriptor_count);
  descriptor = &band->descriptors[producer];
  next_descriptor = &band->descriptors[next_producer];
  if (!wait_for_current_descriptor(forwarder, band, descriptor) ||
      !wait_for_lookahead_descriptor(forwarder, band, next_descriptor)) {
    ++forwarder->full_count;
    return NPU_RUNTIME_FULL;
  }

  record = &band->records[producer];
  prepare_record(record, packet);
  record_address = band->record_physical_base +
                   (uint32_t)producer * NPU_WIFI_TX_BUFFER_SPACE_RECORD_SIZE;
  if (!publish_descriptor(forwarder, band, descriptor, record_address)) {
    ++forwarder->full_count;
    return NPU_RUNTIME_FULL;
  }

  forwarder->producer_state->index[band_index] = next_producer;
  forwarder->last_band = (uint8_t)band_index;
  forwarder->last_band_valid = true;
  ++forwarder->forwarded_packet_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_tdm_tx_forward_publish(void *context) {
  struct npu_wifi_tdm_tx_forward *forwarder = context;
  struct npu_wifi_tdm_tx_band_config *band;

  if (forwarder == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!forwarder->initialized || !forwarder->last_band_valid)
    return NPU_RUNTIME_REJECTED;

  band = &forwarder->band[forwarder->last_band];
  an7581_dma_memory_barrier();
  band->registers->cpu_index =
      forwarder->producer_state->index[forwarder->last_band];
  ++forwarder->cpu_index_publish_count;
  return NPU_RUNTIME_SUCCESS;
}

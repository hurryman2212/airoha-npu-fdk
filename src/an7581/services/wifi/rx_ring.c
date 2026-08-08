/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rx_ring.h"

#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"

#define NPU_WIFI_RX_BUFFER_ADDRESS_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_RX_BUFFER_ADDRESS_ALIAS UINT32_C(0x80000000)
#define NPU_WIFI_RX_EAGLE_CONTROL UINT32_C(0x07000100)
#define NPU_WIFI_RX_MSDU_PAGE_CONTROL UINT32_C(0x00800100)
#define NPU_WIFI_RX_INDICATION_CONTROL UINT32_C(0xe0000000)
#define NPU_WIFI_RX_RXDMAD_C_CONTROL UINT32_C(0xf0000000)

_Static_assert(sizeof(struct npu_wifi_rx_descriptor) ==
                   NPU_WIFI_RX_DESCRIPTOR_SIZE,
               "Wi-Fi RX descriptor layout changed");

#define RX_PROFILE(kind_value, maximum_count, descriptor_bytes, buffer_bytes,  \
                   packet_offset, control, set_id, get_id, allocate, store_id) \
  {                                                                            \
      .kind = (kind_value),                                                    \
      .maximum_descriptor_count = (maximum_count),                             \
      .descriptor_size = (descriptor_bytes),                                   \
      .buffer_stride = (buffer_bytes),                                         \
      .packet_data_offset = (packet_offset),                                   \
      .initial_control = (control),                                            \
      .set_interface = (set_id),                                               \
      .publication_interface = (get_id),                                       \
      .allocates_buffers = (allocate),                                         \
      .stores_buffer_id = (store_id),                                          \
  }

static const struct npu_wifi_rx_ring_profile profiles[] = {
    RX_PROFILE(NPU_WIFI_RX_RING_EAGLE_DATA, NPU_WIFI_RX_DESCRIPTOR_LIMIT,
               NPU_WIFI_RX_DESCRIPTOR_SIZE, NPU_WIFI_RX_PACKET_BUFFER_SIZE,
               UINT32_C(0x00000080), NPU_WIFI_RX_EAGLE_CONTROL,
               NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE,
               NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE, true, true),
    RX_PROFILE(NPU_WIFI_RX_RING_EAGLE_DATA,
               NPU_WIFI_RX_MT7996_SECONDARY_DESCRIPTOR_LIMIT,
               NPU_WIFI_RX_DESCRIPTOR_SIZE, NPU_WIFI_RX_PACKET_BUFFER_SIZE,
               UINT32_C(0x00000080), NPU_WIFI_RX_EAGLE_CONTROL,
               NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE,
               NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE, true, true),
    RX_PROFILE(
        NPU_WIFI_RX_RING_MSDU_PAGE, NPU_WIFI_RX_MT7996_MSDU0_DESCRIPTOR_LIMIT,
        NPU_WIFI_RX_DESCRIPTOR_SIZE, NPU_WIFI_RX_MSDU_PAGE_SIZE, 0U,
        NPU_WIFI_RX_MSDU_PAGE_CONTROL,
        NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_INTERFACE,
        NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_PUBLICATION_INTERFACE, true, true),
    RX_PROFILE(
        NPU_WIFI_RX_RING_MSDU_PAGE, NPU_WIFI_RX_MT7996_MSDU1_DESCRIPTOR_LIMIT,
        NPU_WIFI_RX_DESCRIPTOR_SIZE, NPU_WIFI_RX_MSDU_PAGE_SIZE, 0U,
        NPU_WIFI_RX_MSDU_PAGE_CONTROL,
        NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_INTERFACE,
        NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_PUBLICATION_INTERFACE, true, true),
    RX_PROFILE(
        NPU_WIFI_RX_RING_MSDU_PAGE, NPU_WIFI_RX_MT7996_MSDU2_DESCRIPTOR_LIMIT,
        NPU_WIFI_RX_DESCRIPTOR_SIZE, NPU_WIFI_RX_MSDU_PAGE_SIZE, 0U,
        NPU_WIFI_RX_MSDU_PAGE_CONTROL,
        NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_INTERFACE,
        NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_PUBLICATION_INTERFACE, true, true),
    RX_PROFILE(NPU_WIFI_RX_RING_INDICATION, NPU_WIFI_RX_DESCRIPTOR_LIMIT,
               UINT32_C(8), 0U, 0U, NPU_WIFI_RX_INDICATION_CONTROL,
               NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE,
               NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE, false, false),
    RX_PROFILE(NPU_WIFI_RX_RING_TX_DONE, NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT,
               NPU_WIFI_RX_DESCRIPTOR_SIZE, NPU_WIFI_RX_PACKET_BUFFER_SIZE,
               UINT32_C(0x00000080), NPU_WIFI_RX_EAGLE_CONTROL,
               NPU_WIFI_MT7996_RX_TX_DONE_INTERFACE,
               NPU_WIFI_RX_NO_PUBLICATION_INTERFACE, true, false),
    RX_PROFILE(NPU_WIFI_RX_RING_IGNORED, 0U, 0U, 0U, 0U, 0U,
               NPU_WIFI_MT7996_IGNORED_RX_INTERFACE,
               NPU_WIFI_RX_NO_PUBLICATION_INTERFACE, false, false),
};

static const struct npu_wifi_rx_ring_profile *
find_profile(const struct npu_wifi_rx_ring_profile *entries,
             size_t profile_count, uint32_t set_interface) {
  size_t index;

  for (index = 0U; index < profile_count; ++index) {
    if ((uint32_t)entries[index].set_interface == set_interface)
      return &entries[index];
  }
  return NULL;
}

const struct npu_wifi_rx_ring_profile *
npu_wifi_rx_ring_find_profile(uint32_t set_interface) {
  return find_profile(profiles, sizeof(profiles) / sizeof(profiles[0]),
                      set_interface);
}

static bool profile_is_valid(const struct npu_wifi_rx_ring_profile *profile) {
  if (profile == NULL)
    return false;
  if (profile->kind == NPU_WIFI_RX_RING_IGNORED)
    return profile->maximum_descriptor_count == 0U &&
           profile->descriptor_size == 0U && !profile->allocates_buffers;

  if (profile->maximum_descriptor_count == 0U || profile->descriptor_size == 0U)
    return false;
  if (profile->allocates_buffers)
    return profile->descriptor_size == NPU_WIFI_RX_DESCRIPTOR_SIZE &&
           profile->buffer_stride != 0U;
  return profile->kind == NPU_WIFI_RX_RING_INDICATION ||
         profile->kind == NPU_WIFI_RX_RING_RXDMAD_C;
}

static void release_allocated_buffers(
    uint16_t *buffer_ids, uint32_t allocated_count,
    const struct npu_wifi_rx_buffer_operations *operations, void *context) {
  while (allocated_count != 0U) {
    --allocated_count;
    operations->release(context, buffer_ids[allocated_count]);
    buffer_ids[allocated_count] = UINT16_MAX;
  }
}

static enum npu_runtime_result initialize_buffer_descriptors(
    const struct npu_wifi_rx_ring_profile *profile, uint32_t packet_buffer_base,
    void *descriptor_memory, uint32_t descriptor_count, uint16_t *buffer_ids,
    const struct npu_wifi_rx_buffer_operations *operations, void *context) {
  struct npu_wifi_rx_descriptor *descriptors = descriptor_memory;
  uint32_t index;

  for (index = 0U; index < descriptor_count; ++index)
    buffer_ids[index] = UINT16_MAX;

  for (index = 0U; index < descriptor_count; ++index) {
    uint16_t buffer_id;
    uint32_t packet_address;
    uint32_t packet_offset;

    if (!operations->allocate(context, &buffer_id)) {
      release_allocated_buffers(buffer_ids, index, operations, context);
      (void)npu_memset(descriptor_memory, 0U,
                       (size_t)descriptor_count * profile->descriptor_size);
      return NPU_RUNTIME_EMPTY;
    }

    buffer_ids[index] = buffer_id;
    packet_offset = (uint32_t)buffer_id * profile->buffer_stride;
    if (packet_buffer_base > UINT32_MAX - packet_offset) {
      release_allocated_buffers(buffer_ids, index + 1U, operations, context);
      (void)npu_memset(descriptor_memory, 0U,
                       (size_t)descriptor_count * profile->descriptor_size);
      return NPU_RUNTIME_OUT_OF_RANGE;
    }

    packet_address = ((packet_buffer_base + packet_offset) &
                      NPU_WIFI_RX_BUFFER_ADDRESS_MASK) |
                     NPU_WIFI_RX_BUFFER_ADDRESS_ALIAS;
    if (packet_address > UINT32_MAX - profile->packet_data_offset) {
      release_allocated_buffers(buffer_ids, index + 1U, operations, context);
      (void)npu_memset(descriptor_memory, 0U,
                       (size_t)descriptor_count * profile->descriptor_size);
      return NPU_RUNTIME_OUT_OF_RANGE;
    }

    descriptors[index].buffer_address =
        packet_address + profile->packet_data_offset;
    descriptors[index].control = profile->initial_control;
    descriptors[index].buffer_id =
        profile->stores_buffer_id ? (uint32_t)buffer_id << 16U : 0U;
    descriptors[index].sequence_control = 0U;
  }
  return NPU_RUNTIME_SUCCESS;
}

static void
prime_existing_descriptors(const struct npu_wifi_rx_ring_profile *profile,
                           void *descriptor_memory, uint32_t descriptor_count) {
  uint8_t *descriptor = descriptor_memory;
  uint32_t index;
  uint32_t control_offset =
      profile->kind == NPU_WIFI_RX_RING_INDICATION ? 4U : 12U;

  for (index = 0U; index < descriptor_count; ++index) {
    uint32_t *control = (uint32_t *)(void *)(descriptor + control_offset);

    *control |= profile->initial_control;
    descriptor += profile->descriptor_size;
  }
}

enum npu_runtime_result npu_wifi_rx_ring_initialize(
    const struct npu_wifi_rx_ring_profile *profile, uint32_t packet_buffer_base,
    void *descriptor_memory, size_t descriptor_memory_size,
    uint32_t descriptor_count, uint16_t *buffer_ids,
    uint32_t buffer_id_capacity,
    const struct npu_wifi_rx_buffer_operations *operations, void *context) {
  size_t required_size;

  if (!profile_is_valid(profile))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (profile->kind == NPU_WIFI_RX_RING_IGNORED)
    return NPU_RUNTIME_SUCCESS;
  if (descriptor_memory == NULL ||
      ((uintptr_t)descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      descriptor_count == 0U ||
      descriptor_count > profile->maximum_descriptor_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  required_size = (size_t)descriptor_count * profile->descriptor_size;
  if (descriptor_memory_size < required_size)
    return NPU_RUNTIME_OUT_OF_RANGE;

  if (!profile->allocates_buffers) {
    prime_existing_descriptors(profile, descriptor_memory, descriptor_count);
    return NPU_RUNTIME_SUCCESS;
  }

  if (buffer_ids == NULL ||
      ((uintptr_t)buffer_ids & (sizeof(uint16_t) - 1U)) != 0U ||
      buffer_id_capacity < descriptor_count || operations == NULL ||
      operations->allocate == NULL || operations->release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  return initialize_buffer_descriptors(profile, packet_buffer_base,
                                       descriptor_memory, descriptor_count,
                                       buffer_ids, operations, context);
}

static bool msdu_page_configuration_is_valid(
    const struct npu_wifi_msdu_page_descriptor_state *state,
    const struct npu_wifi_msdu_page_descriptor_config *config,
    const struct npu_wifi_rx_ring_profile *profile, uint32_t descriptor_count) {
  uint32_t required_capacity = descriptor_count;
  size_t required_descriptor_size;

  if (profile == NULL || profile->kind != NPU_WIFI_RX_RING_MSDU_PAGE ||
      descriptor_count == 0U ||
      descriptor_count > profile->maximum_descriptor_count ||
      config->descriptor_memory == NULL || config->page_ids == NULL ||
      config->page_id_operations.allocate == NULL ||
      config->page_id_operations.release == NULL)
    return false;

  if (((uintptr_t)config->descriptor_memory & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)config->page_ids & (sizeof(uint16_t) - 1U)) != 0U)
    return false;

  if (state->ready)
    return false;

  required_descriptor_size =
      (size_t)required_capacity * profile->descriptor_size;
  return config->page_id_capacity >= required_capacity &&
         config->descriptor_memory_size >= required_descriptor_size;
}

enum npu_runtime_result npu_wifi_msdu_page_descriptors_initialize(
    struct npu_wifi_msdu_page_descriptor_state *state,
    const struct npu_wifi_msdu_page_descriptor_config *config,
    uint32_t descriptor_count) {
  const struct npu_wifi_rx_ring_profile *profile;
  enum npu_runtime_result status;

  if (state == NULL || config == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  profile = npu_wifi_rx_ring_find_profile(config->set_interface);
  if (!msdu_page_configuration_is_valid(state, config, profile,
                                        descriptor_count))
    return state->ready && profile != NULL ? NPU_RUNTIME_REJECTED
                                           : NPU_RUNTIME_OUT_OF_RANGE;

  *state = (struct npu_wifi_msdu_page_descriptor_state){0};
  status = npu_wifi_rx_ring_initialize(
      profile, config->page_pool_base, config->descriptor_memory,
      config->descriptor_memory_size, descriptor_count, config->page_ids,
      config->page_id_capacity, &config->page_id_operations,
      config->operation_context);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  state->descriptor_count = descriptor_count;
  state->set_interface = (uint8_t)config->set_interface;
  state->ready = true;
  return NPU_RUNTIME_SUCCESS;
}

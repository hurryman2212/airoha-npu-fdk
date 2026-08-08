/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/wifi_mt7996_rx_refill_lifecycle.h"

#include "an7581/runtime/memory.h"
#include "an7581/services/wifi/rx_pcie.h"

#define AN7581_WIFI_MT7996_RX_REFILL_HOST_ADDRESS_LIMIT UINT32_C(0xbfffffff)

struct refill_ring_requirement {
  uint8_t set_interface;
  uint32_t requirement;
};

static const struct refill_ring_requirement ring_requirements[] = {
    {NPU_WIFI_MT7996_RX_RRO_BAND0_INTERFACE,
     AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_RRO_BAND0},
    {NPU_WIFI_MT7996_RX_RRO_BAND2_INTERFACE,
     AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_RRO_BAND2},
    {NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND0_INTERFACE,
     AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BAND0},
    {NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND1_INTERFACE,
     AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BAND1},
    {NPU_WIFI_MT7996_RX_MSDU_PAGE_BAND2_INTERFACE,
     AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BAND2},
};

_Static_assert(sizeof(ring_requirements) / sizeof(ring_requirements[0]) ==
                   AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT,
               "MT7996 RX-refill ring count changed");

static bool
interface_is_ready(const struct npu_wifi_interface_configuration *interface,
                   const struct npu_wifi_rx_ring_profile *profile) {
  return profile != NULL &&
         (interface->valid_fields &
          (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT)) ==
             (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT) &&
         interface->pcie_address != 0U &&
         interface->pcie_address <=
             AN7581_WIFI_MT7996_RX_REFILL_HOST_ADDRESS_LIMIT -
                 NPU_WIFI_RX_PCIE_DMA_INDEX_OFFSET &&
         (interface->pcie_address & (sizeof(uint32_t) - 1U)) == 0U &&
         interface->descriptor_count != 0U &&
         interface->descriptor_count <= profile->maximum_descriptor_count;
}

enum npu_runtime_result an7581_wifi_mt7996_rx_refill_configuration_readiness(
    const struct npu_wifi_configuration *configuration,
    struct an7581_wifi_mt7996_rx_refill_configuration_readiness *readiness) {
  size_t index;

  if (configuration == NULL || readiness == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  (void)npu_memset(readiness, 0U, sizeof(*readiness));
  if (!configuration->packet_buffer_address_valid) {
    readiness->missing |= AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_PACKET_BUFFER;
  } else if (configuration->packet_buffer_address == 0U ||
             (configuration->packet_buffer_address &
              (NPU_WIFI_RX_PACKET_BUFFER_SIZE - 1U)) != 0U) {
    readiness->invalid |= AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_PACKET_BUFFER;
  }
  if (!configuration->dram_ba_node_address_valid) {
    readiness->missing |= AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BUFFER;
  } else if (configuration->dram_ba_node_address == 0U ||
             (configuration->dram_ba_node_address &
              (NPU_WIFI_RX_MSDU_PAGE_SIZE - 1U)) != 0U) {
    readiness->invalid |= AN7581_WIFI_MT7996_RX_REFILL_REQUIRED_MSDU_BUFFER;
  }

  for (index = 0U;
       index < sizeof(ring_requirements) / sizeof(ring_requirements[0]);
       ++index) {
    const struct refill_ring_requirement *requirement =
        &ring_requirements[index];
    const struct npu_wifi_interface_configuration *interface =
        &configuration->interface[requirement->set_interface];
    const struct npu_wifi_rx_ring_profile *profile =
        npu_wifi_rx_ring_find_profile(requirement->set_interface);

    if ((interface->valid_fields &
         (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT)) !=
        (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT))
      readiness->missing |= requirement->requirement;
    else if (!interface_is_ready(interface, profile))
      readiness->invalid |= requirement->requirement;
  }

  if (readiness->invalid != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (readiness->missing != 0U)
    return NPU_RUNTIME_EMPTY;
  return NPU_RUNTIME_SUCCESS;
}

static bool operations_are_valid(
    const struct an7581_wifi_mt7996_rx_refill_operations *operations) {
  return operations != NULL && operations->read32 != NULL &&
         operations->write32 != NULL && operations->wake_worker != NULL;
}

enum npu_runtime_result an7581_wifi_mt7996_rx_refill_lifecycle_initialize(
    struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
    const struct an7581_wifi_mt7996_rx_refill_lifecycle_config *config) {
  size_t index;

  if (lifecycle == NULL || config == NULL || config->configuration == NULL ||
      config->control_lifecycle == NULL || config->dispatch == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!config->dispatch->initialized || config->dispatch->worker != NULL ||
      config->dispatch->worker_context != NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (config->activation_allowed && !operations_are_valid(config->operations))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(lifecycle, 0U, sizeof(*lifecycle));
  lifecycle->configuration = config->configuration;
  lifecycle->control_lifecycle = config->control_lifecycle;
  lifecycle->dispatch = config->dispatch;
  lifecycle->operations = config->operations;
  lifecycle->operation_context = config->operation_context;
  for (index = 0U; index < AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT; ++index)
    lifecycle->diagnostic_counters[index] = config->diagnostic_counters[index];
  lifecycle->state =
      AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
  lifecycle->last_status = NPU_RUNTIME_EMPTY;
  lifecycle->activation_allowed = config->activation_allowed;
  lifecycle->initialized = true;
  return NPU_RUNTIME_SUCCESS;
}

static struct npu_wifi_rx_arena *
find_rx_arena(struct npu_wifi_mt7996_control_plane *control_plane,
              uint32_t set_interface) {
  size_t index;

  for (index = 0U; index < control_plane->rx_backend.arena_count; ++index) {
    struct npu_wifi_rx_arena *arena = &control_plane->rx_backend.arenas[index];

    if ((uint32_t)arena->set_interface == set_interface)
      return arena;
  }
  return NULL;
}

static enum npu_runtime_result
bind_ring(struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
          struct npu_wifi_mt7996_control_plane *control_plane,
          size_t ring_index) {
  const uint32_t set_interface = ring_requirements[ring_index].set_interface;
  const struct npu_wifi_interface_configuration *interface =
      &lifecycle->configuration->interface[set_interface];
  struct npu_wifi_rx_arena *arena = find_rx_arena(control_plane, set_interface);
  const struct npu_wifi_rx_buffer_operations *buffer_operations;
  void *buffer_context;
  uint32_t packet_buffer_base;
  enum npu_runtime_result status;

  if (arena == NULL || arena->descriptor_memory == NULL ||
      arena->buffer_ids == NULL ||
      arena->initialized_descriptor_count != interface->descriptor_count ||
      arena->buffer_id_capacity < interface->descriptor_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  buffer_operations = arena->buffer_operations != NULL
                          ? arena->buffer_operations
                          : control_plane->rx_backend.buffer_operations;
  buffer_context = arena->buffer_operations != NULL
                       ? arena->buffer_context
                       : control_plane->rx_backend.buffer_context;
  if (buffer_operations == NULL || buffer_operations->allocate == NULL ||
      buffer_operations->release == NULL || buffer_context == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  packet_buffer_base = set_interface < 5U
                           ? lifecycle->configuration->packet_buffer_address
                           : lifecycle->configuration->dram_ba_node_address;
  if (arena->packet_buffer_base != packet_buffer_base)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = npu_wifi_rx_refill_initialize(
      &lifecycle->states[ring_index], set_interface, arena->descriptor_memory,
      arena->descriptor_memory_size, arena->buffer_ids,
      arena->buffer_id_capacity, arena->initialized_descriptor_count,
      &lifecycle->diagnostic_counters[ring_index], packet_buffer_base,
      interface->pcie_address);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  lifecycle->rings[ring_index] = (struct npu_wifi_rx_refill_worker_ring){
      .state = &lifecycle->states[ring_index],
      .buffer_operations = buffer_operations,
      .buffer_context = buffer_context,
      .write32 = lifecycle->operations->write32,
      .write_context = lifecycle->operation_context,
  };
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
bind_rings(struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle) {
  struct an7581_wifi_mt7996_rro_control_platform *platform =
      lifecycle->control_lifecycle->platform;
  struct npu_wifi_mt7996_control_plane *control_plane;
  size_t index;

  if (platform == NULL || !platform->initialized)
    return NPU_RUNTIME_OUT_OF_RANGE;
  control_plane = &platform->control_plane;
  if (!control_plane->initialized ||
      control_plane->rx_backend.arenas != control_plane->rx_arenas ||
      control_plane->rx_backend.arena_count !=
          NPU_WIFI_MT7996_CONTROL_RX_ARENA_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  for (index = 0U; index < AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT; ++index) {
    enum npu_runtime_result status = bind_ring(lifecycle, control_plane, index);

    if (status != NPU_RUNTIME_SUCCESS) {
      (void)npu_memset(lifecycle->states, 0U, sizeof(lifecycle->states));
      (void)npu_memset(lifecycle->rings, 0U, sizeof(lifecycle->rings));
      return status;
    }
  }
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
refill_worker_step(void *context, struct an7581_core1_worker_result *result) {
  struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle = context;
  struct npu_wifi_rx_refill_worker_operations worker_operations;
  struct npu_wifi_rx_refill_worker_result worker_result;
  size_t index;
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  ++lifecycle->worker_step_count;

  for (index = 0U; index < AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT; ++index) {
    struct npu_wifi_rx_refill_worker_ring *ring = &lifecycle->rings[index];
    uint32_t dma_index;

    if (!lifecycle->operations->read32(lifecycle->operation_context,
                                       ring->state->register_base +
                                           NPU_WIFI_RX_PCIE_DMA_INDEX_OFFSET,
                                       &dma_index) ||
        dma_index >= ring->state->descriptor_count) {
      ++lifecycle->worker_failure_count;
      result->should_backoff = true;
      return NPU_RUNTIME_IO_ERROR;
    }
    ring->dma_index = dma_index;
  }

  worker_operations = (struct npu_wifi_rx_refill_worker_operations){
      .event = lifecycle->operations->event,
      .delay = lifecycle->operations->delay,
      .context = lifecycle->operation_context,
  };
  status = npu_wifi_rx_refill_worker_cycle(
      lifecycle->rings, AN7581_WIFI_MT7996_RX_REFILL_RING_COUNT,
      &worker_operations, &worker_result);
  result->should_backoff = status != NPU_RUNTIME_SUCCESS;
  if (status != NPU_RUNTIME_SUCCESS && status != NPU_RUNTIME_EMPTY)
    ++lifecycle->worker_failure_count;
  return status;
}

static void lifecycle_result_update(
    const struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rx_refill_lifecycle_result *result) {
  result->state = lifecycle->state;
  result->status = lifecycle->last_status;
  result->rings_bound = lifecycle->rings_bound;
  result->worker_published = lifecycle->worker_published;
  result->worker_woken = lifecycle->worker_woken;
  result->active =
      lifecycle->state == AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_ACTIVE;
}

static enum npu_runtime_result lifecycle_retryable_failure(
    struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rx_refill_lifecycle_result *result,
    enum npu_runtime_result status) {
  lifecycle->state = AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_RETRYABLE_FAILURE;
  lifecycle->last_status = status;
  ++lifecycle->retryable_failure_count;
  lifecycle_result_update(lifecycle, result);
  return status;
}

enum npu_runtime_result an7581_wifi_mt7996_rx_refill_lifecycle_step(
    struct an7581_wifi_mt7996_rx_refill_lifecycle *lifecycle,
    struct an7581_wifi_mt7996_rx_refill_lifecycle_result *result) {
  enum npu_runtime_result status;

  if (lifecycle == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  (void)npu_memset(result, 0U, sizeof(*result));
  if (!lifecycle->initialized || lifecycle->configuration == NULL ||
      lifecycle->control_lifecycle == NULL || lifecycle->dispatch == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  ++lifecycle->step_count;
  status = an7581_wifi_mt7996_rx_refill_configuration_readiness(
      lifecycle->configuration, &result->readiness);
  if (status != NPU_RUNTIME_SUCCESS) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAITING_FOR_CONFIGURATION;
    lifecycle->last_status = status;
    ++lifecycle->configuration_wait_count;
    result->waiting_for_configuration = true;
    lifecycle_result_update(lifecycle, result);
    return status;
  }
  if (!lifecycle->activation_allowed) {
    lifecycle->state = AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_ACTIVATION_GATED;
    lifecycle->last_status = NPU_RUNTIME_REJECTED;
    ++lifecycle->activation_gate_count;
    result->activation_gated = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }
  if (!operations_are_valid(lifecycle->operations))
    return lifecycle_retryable_failure(lifecycle, result,
                                       NPU_RUNTIME_OUT_OF_RANGE);

  if (lifecycle->control_lifecycle->state !=
          AN7581_WIFI_MT7996_RRO_CONTROL_LIFECYCLE_ACTIVE ||
      !lifecycle->control_lifecycle->control_plane_initialized) {
    lifecycle->state =
        AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAITING_FOR_CONTROL_PLANE;
    lifecycle->last_status = NPU_RUNTIME_EMPTY;
    ++lifecycle->control_plane_wait_count;
    result->waiting_for_control_plane = true;
    lifecycle_result_update(lifecycle, result);
    return lifecycle->last_status;
  }

  if (!lifecycle->rings_bound) {
    lifecycle->state = AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_BINDING_RINGS;
    ++lifecycle->ring_bind_attempt_count;
    status = bind_rings(lifecycle);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->rings_bound = true;
  }

  if (!lifecycle->worker_published) {
    lifecycle->state = AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_PUBLISHING;
    ++lifecycle->publication_attempt_count;
    status = an7581_core1_dispatch_publish(lifecycle->dispatch,
                                           refill_worker_step, lifecycle);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->worker_published = true;
  }

  if (!lifecycle->worker_woken) {
    lifecycle->state = AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_WAKING_WORKER;
    ++lifecycle->wake_attempt_count;
    status = lifecycle->operations->wake_worker(lifecycle->operation_context,
                                                AN7581_CORE1_HART_MASK);
    if (status != NPU_RUNTIME_SUCCESS)
      return lifecycle_retryable_failure(lifecycle, result, status);
    lifecycle->worker_woken = true;
  }

  lifecycle->state = AN7581_WIFI_MT7996_RX_REFILL_LIFECYCLE_ACTIVE;
  lifecycle->last_status = NPU_RUNTIME_SUCCESS;
  lifecycle_result_update(lifecycle, result);
  return NPU_RUNTIME_SUCCESS;
}

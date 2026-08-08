/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/arch/riscv/cache.h"
#include "an7581/platform/core1_dispatch.h"
#include "an7581/platform/core2_dispatch.h"
#include "an7581/platform/core4_dispatch.h"
#include "an7581/platform/core56_dispatch.h"
#include "an7581/platform/core7_dispatch.h"
#include "an7581/platform/data_image.h"
#include "an7581/platform/dma.h"
#include "an7581/platform/hardware_mutex.h"
#include "an7581/platform/mailbox.h"
#include "an7581/platform/memory_initialization.h"
#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/plic.h"
#include "an7581/platform/ppe.h"
#include "an7581/platform/system_initialization.h"
#include "an7581/platform/timer.h"
#include "an7581/platform/tr471_board_binding.h"
#include "an7581/platform/tr471_lifecycle.h"
#include "an7581/platform/tr471_runtime_lifecycle.h"
#include "an7581/platform/tunnel.h"
#include "an7581/platform/wifi_mt7996_completion_board_binding.h"
#include "an7581/platform/wifi_mt7996_completion_dispatch.h"
#include "an7581/platform/wifi_mt7996_completion_lifecycle.h"
#include "an7581/platform/wifi_mt7996_data_plane_stop.h"
#include "an7581/platform/wifi_mt7996_ppe_result_board_binding.h"
#include "an7581/platform/wifi_mt7996_ppe_result_bundle.h"
#include "an7581/platform/wifi_mt7996_rro_control_board_binding.h"
#include "an7581/platform/wifi_mt7996_rro_control_lifecycle.h"
#include "an7581/platform/wifi_mt7996_rx_refill_board_binding.h"
#include "an7581/platform/wifi_mt7996_rx_refill_lifecycle.h"
#include "an7581/platform/wifi_mt7996_tx_fast_path_board_binding.h"
#include "an7581/platform/wifi_mt7996_tx_fast_path_lifecycle.h"
#include "an7581/runtime/memory.h"
#include "an7581/runtime/panic.h"
#include "an7581/services/wifi/compatibility.h"
#include "an7581/services/wifi/mt7996_mailbox_interface.h"

#define AN7581_WIFI_MT7996_RRO_PAGE_COUNT UINT32_C(0x4000)
#define AN7581_WIFI_MT7996_RRO_PACKET_COUNT                                    \
  NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT

/*
 * The host driver stops and restarts WED/WFDMA around the mailbox stop and
 * reinitialization requests.  No additional board transition is performed by
 * the NPU after those requests reach an individual data-plane component.
 */
static enum npu_runtime_result
acknowledge_host_data_plane_transition(void *context) {
  (void)context;
  return NPU_RUNTIME_SUCCESS;
}

/* Worker harts continuously poll their dispatch loops, so no IPI is needed. */
static enum npu_runtime_result polling_workers_are_active(void *context,
                                                          uint32_t hart_mask) {
  (void)context;
  return hart_mask != 0U &&
                 (hart_mask & ~((UINT32_C(1) << AN7581_NPU_CORE_COUNT) - 1U)) ==
                     0U
             ? NPU_RUNTIME_SUCCESS
             : NPU_RUNTIME_OUT_OF_RANGE;
}

static bool board_mmio_read32(void *context, uint32_t address,
                              uint32_t *value) {
  (void)context;
  if (value == NULL || address == 0U ||
      (address & (sizeof(uint32_t) - 1U)) != 0U)
    return false;

  *value = an7581_mmio_read32(address);
  return true;
}

static bool board_mmio_write32(void *context, uint32_t address,
                               uint32_t value) {
  (void)context;
  if (address == 0U || (address & (sizeof(uint32_t) - 1U)) != 0U)
    return false;

  an7581_mmio_write32(address, value);
  return true;
}

static struct npu_wifi_mt7996_rro_pipeline_config rro_pipeline_config;
static struct npu_wifi_mt7996_control_plane_config rro_control_plane_config;
static volatile struct npu_wifi_mt7996_band0_diagnostic_counters
    *g_band0_diagnostic_counters;
static volatile struct npu_wifi_mt7996_band1_diagnostic_counters
    *g_band1_diagnostic_counters;

static const struct an7581_wifi_mt7996_rro_control_board_binding
    rro_control_board_binding = {
        .pipeline = &rro_pipeline_config,
        .control_plane = &rro_control_plane_config,
        .wake_workers = polling_workers_are_active,
        .prepare_stop = acknowledge_host_data_plane_transition,
        .resume = acknowledge_host_data_plane_transition,
        .activation_allowed = true,
};

static void rx_refill_event(void *context,
                            enum npu_wifi_rx_refill_worker_event event) {
  (void)context;
  if (event == NPU_WIFI_RX_REFILL_WORKER_EAGLE_CYCLE &&
      g_band0_diagnostic_counters != NULL)
    ++g_band0_diagnostic_counters->rx_refill_eagle_cycles;
  else if (event == NPU_WIFI_RX_REFILL_WORKER_MSDU_CYCLE &&
           g_band1_diagnostic_counters != NULL)
    ++g_band1_diagnostic_counters->rx_refill_msdu_cycles;
}

static void rx_refill_delay(void *context, uint32_t iterations) {
  (void)context;
  while (iterations != 0U) {
    an7581_cpu_relax();
    --iterations;
  }
}

static const struct an7581_wifi_mt7996_rx_refill_operations
    rx_refill_operations = {
        .read32 = board_mmio_read32,
        .write32 = board_mmio_write32,
        .event = rx_refill_event,
        .delay = rx_refill_delay,
        .wake_worker = polling_workers_are_active,
};

static const struct an7581_wifi_mt7996_rx_refill_board_binding
    rx_refill_board_binding = {
        .operations = &rx_refill_operations,
        .prepare_stop = acknowledge_host_data_plane_transition,
        .resume = acknowledge_host_data_plane_transition,
        .activation_allowed = true,
};

static const struct an7581_wifi_mt7996_completion_board_binding
    completion_board_binding = {
        .wake_workers = polling_workers_are_active,
        .prepare_stop = acknowledge_host_data_plane_transition,
        .resume = acknowledge_host_data_plane_transition,
        .vdma_poll_limit =
            AN7581_WIFI_MT7996_COMPLETION_DEFAULT_VDMA_POLL_LIMIT,
        .tx_done_budget = NPU_WIFI_MT7996_TX_DONE_PROCESS_LIMIT,
        .band0_budget = NPU_WIFI_MT7996_COMPLETION_BAND0_BUDGET,
        .packet_queue_producer = 0U,
        .packet_queue_consumers = {0U, 0U},
        .activation_allowed = true,
};

static const struct an7581_wifi_mt7996_tx_fast_path_board_binding
    tx_fast_path_board_binding = {
        .wake_worker = polling_workers_are_active,
        .prepare_stop = acknowledge_host_data_plane_transition,
        .resume = acknowledge_host_data_plane_transition,
        .vdma_poll_limit =
            AN7581_WIFI_MT7996_TX_FAST_PATH_DEFAULT_VDMA_POLL_LIMIT,
        .activation_allowed = true,
};

static const struct an7581_wifi_mt7996_ppe_result_board_binding
    ppe_result_board_binding = {
        .hart_id = AN7581_WIFI_MT7996_PPE_RESULT_HART,
        .packet_queue_producer = 0U,
        .fragment_queue_producer = 0U,
        .prepare_stop = acknowledge_host_data_plane_transition,
        .resume = acknowledge_host_data_plane_transition,
        .activation_allowed = true,
};

static struct an7581_tr471_board_binding tr471_board_binding = {
    .wake_harts = polling_workers_are_active,
    .transmit_budget = NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT,
    .receive_budget = NPU_TR471_RUNTIME_PACKET_BUDGET_LIMIT,
    .shared_buffer_extent = NPU_TR471_TDMA_SHARED_BUFFER_MINIMUM_EXTENT,
    .activation_allowed = true,
};

static struct npu_firmware_state g_firmware_state;
static struct an7581_data_configuration g_data_configuration;
static struct an7581_mailbox_runtime g_mailbox_runtime;
static struct an7581_ppe_runtime g_ppe_runtime;
static struct an7581_hardware_mutex_bank g_sram_allocator_mutex;
static struct an7581_core2_dispatch g_core2_dispatch;
static struct an7581_core56_dispatch g_core56_dispatch;
static struct an7581_tr471_runtime_dispatch g_tr471_runtime_dispatch;
static struct an7581_core7_dispatch g_core7_dispatch;
static struct an7581_tunnel_platform g_tunnel_platform;
static struct an7581_wifi_mt7996_completion_dispatch g_completion_dispatch;
static struct an7581_wifi_mt7996_runtime_readiness_state g_wifi_readiness;
static struct an7581_system_clock_rates g_system_clock_rates;
static volatile bool g_platform_interrupts_ready;

static enum npu_runtime_result
core4_mt7996_worker_step(void *context,
                         struct an7581_core4_worker_result *result) {
  struct an7581_wifi_mt7996_completion_dispatch_result completion_result;
  enum npu_runtime_result status;

  status = an7581_wifi_mt7996_completion_dispatch_step(
      context, AN7581_WIFI_MT7996_COMPLETION_TX_DONE_HART, &completion_result);
  result->should_backoff = completion_result.should_backoff;
  return status;
}

static struct an7581_core1_dispatch g_core1_dispatch;
static struct an7581_core4_dispatch g_core4_dispatch = {
    .worker = core4_mt7996_worker_step,
    .worker_context = &g_completion_dispatch,
    .initialized = true,
};
static struct an7581_wifi_mt7996_tx_fast_path_platform g_tx_fast_path_platform;
static struct an7581_wifi_mt7996_tx_fast_path_lifecycle
    g_tx_fast_path_lifecycle;
static struct an7581_wifi_mt7996_completion_platform g_completion_platform;
static struct an7581_wifi_mt7996_completion_lifecycle g_completion_lifecycle;
static struct an7581_wifi_mt7996_ppe_result_lifecycle g_ppe_result_lifecycle;
static struct an7581_wifi_mt7996_rro_control_platform g_rro_control_platform;
static struct an7581_wifi_mt7996_rro_control_lifecycle g_rro_control_lifecycle;
static struct an7581_wifi_mt7996_rx_refill_lifecycle g_rx_refill_lifecycle;
static struct an7581_wifi_mt7996_data_plane_stop g_data_plane_stop;
static struct an7581_tr471_lifecycle_platform g_tr471_platform;
static struct an7581_tr471_lifecycle g_tr471_lifecycle;
static struct an7581_tr471_runtime_platform g_tr471_runtime_platform;
static struct an7581_tr471_runtime_lifecycle g_tr471_runtime_lifecycle;
static volatile struct npu_wifi_rro_metadata_table_entry
    *g_rro_normal_groups[NPU_WIFI_RRO_NORMAL_TABLE_GROUP_LIMIT];

enum wifi_data_plane_transition {
  WIFI_DATA_PLANE_RUNNING = 0,
  WIFI_DATA_PLANE_STOPPING,
  WIFI_DATA_PLANE_STOPPED,
  WIFI_DATA_PLANE_RESTARTING,
};

static enum wifi_data_plane_transition g_wifi_data_plane_transition;

static bool wifi_request_is_inode_action(uint32_t action) {
  const struct npu_wifi_last_request *request =
      &g_firmware_state.wifi.last_request;

  return g_firmware_state.wifi.last_request_valid &&
         request->command == NPU_WIFI_SET_INODE_TXRX_REG_ADDR &&
         (request->operation == NPU_WIFI_OPERATION_SET ||
          request->operation == NPU_WIFI_OPERATION_SET_NO_WAIT) &&
         request->interface == action;
}

static void publish_wifi_runtime_readiness(void) {
  const struct npu_wifi_rro_control *rro_control =
      &g_rro_control_platform.pipeline.control;
  bool tx_configuration_ready = g_wifi_readiness.tx_configuration_state ==
                                NPU_WIFI_MT7996_COMPLETION_READY_STATE;
  bool offload_initialized;
  bool tx_done_enabled;

  if (wifi_request_is_inode_action(NPU_WIFI_RRO_INODE_STOP))
    tx_configuration_ready = false;
  else if (wifi_request_is_inode_action(NPU_WIFI_RRO_INODE_RESUME))
    tx_configuration_ready = true;

  offload_initialized = g_completion_lifecycle.pipeline_initialized &&
                        g_rro_control_lifecycle.control_plane_initialized &&
                        g_tx_fast_path_lifecycle.fast_path_initialized &&
                        g_ppe_result_lifecycle.bundle.initialized;
  tx_done_enabled = g_rro_control_lifecycle.control_plane_initialized &&
                    g_rro_control_platform.control_plane.tx_done_state.ready &&
                    !rro_control->rx_stopped;

  (void)an7581_wifi_mt7996_runtime_publish_offload(&g_wifi_readiness,
                                                   offload_initialized);
  (void)an7581_wifi_mt7996_runtime_publish_tx_state(
      &g_wifi_readiness, tx_done_enabled, tx_configuration_ready);
  (void)an7581_wifi_mt7996_runtime_publish_rx_state(
      &g_wifi_readiness, rro_control->ring_enabled != 0U,
      rro_control->configuration_ready != 0U);
  (void)an7581_wifi_mt7996_runtime_publish_host_rx_state(
      &g_wifi_readiness, g_completion_lifecycle.pipeline_initialized);
}

static bool wifi_data_plane_transition_pending(void) {
  return g_wifi_data_plane_transition == WIFI_DATA_PLANE_STOPPING ||
         g_wifi_data_plane_transition == WIFI_DATA_PLANE_RESTARTING;
}

static void wifi_data_plane_transition_progress(void) {
  struct an7581_wifi_mt7996_data_plane_stop_result result;
  enum npu_runtime_result status;

  if (g_wifi_data_plane_transition == WIFI_DATA_PLANE_STOPPING) {
    g_firmware_state.wifi.npu_information.worker_status = 1U;
    status =
        an7581_wifi_mt7996_data_plane_stop_step(&g_data_plane_stop, &result);
    if (status == NPU_RUNTIME_SUCCESS && result.stopped)
      status = npu_wifi_rro_control_finish_stop(
          &g_rro_control_platform.pipeline.control);
    if (status == NPU_RUNTIME_SUCCESS && result.stopped) {
      g_wifi_data_plane_transition = WIFI_DATA_PLANE_STOPPED;
      g_firmware_state.wifi.npu_information.worker_status = 0U;
    }
    an7581_dma_memory_barrier();
    return;
  }
  if (g_wifi_data_plane_transition != WIFI_DATA_PLANE_RESTARTING)
    return;

  status =
      an7581_wifi_mt7996_data_plane_restart_step(&g_data_plane_stop, &result);
  if (status == NPU_RUNTIME_SUCCESS && result.restarted &&
      an7581_wifi_mt7996_data_plane_stop_reset(&g_data_plane_stop) ==
          NPU_RUNTIME_SUCCESS) {
    g_wifi_data_plane_transition = WIFI_DATA_PLANE_RUNNING;
    g_firmware_state.wifi.npu_information.worker_status = 1U;
    publish_wifi_runtime_readiness();
  }
  an7581_dma_memory_barrier();
}

static void wifi_data_plane_handle_request(void) {
  struct npu_wifi_mt7996_control_plane *control_plane =
      &g_rro_control_platform.control_plane;

  if (wifi_request_is_inode_action(NPU_WIFI_RRO_INODE_STOP) &&
      g_wifi_data_plane_transition == WIFI_DATA_PLANE_RUNNING) {
    g_wifi_data_plane_transition = WIFI_DATA_PLANE_STOPPING;
  } else if (wifi_request_is_inode_action(
                 NPU_WIFI_RRO_INODE_RESET_BUFFER_IDS) &&
             g_wifi_data_plane_transition == WIFI_DATA_PLANE_STOPPED) {
    enum npu_runtime_result status =
        npu_wifi_mt7996_control_plane_prepare_reinitialization(control_plane);

    if (status != NPU_RUNTIME_SUCCESS)
      an7581_panic(AN7581_PANIC_CONFIGURATION);
  } else if (wifi_request_is_inode_action(NPU_WIFI_RRO_INODE_RESUME) &&
             g_wifi_data_plane_transition == WIFI_DATA_PLANE_STOPPED) {
    if (an7581_wifi_mt7996_completion_pipeline_refresh_host_rx_memory(
            &g_completion_platform.pipeline) != NPU_RUNTIME_SUCCESS)
      an7581_panic(AN7581_PANIC_CONFIGURATION);
    g_wifi_data_plane_transition = WIFI_DATA_PLANE_RESTARTING;
  }
}

static enum npu_runtime_result
rro_map_table(void *context, uint32_t physical_address, uint32_t length,
              volatile struct npu_wifi_rro_metadata_table_entry **entries) {
  uint32_t local_address;

  (void)context;
  if (entries == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!an7581_dma_buffer_map(physical_address, length, sizeof(uint32_t),
                             &local_address))
    return NPU_RUNTIME_OUT_OF_RANGE;

  *entries = (volatile struct npu_wifi_rro_metadata_table_entry *)(uintptr_t)
      local_address;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result rro_discard_cache(void *context,
                                                 uint32_t line_address) {
  (void)context;
  if (line_address == 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  an7581_l1_dcache_discard((const void *)(uintptr_t)line_address);
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result rro_publish_cursor(void *context,
                                                  uint32_t cursor_value) {
  const struct npu_wifi_configuration *configuration = context;
  const struct npu_wifi_interface_configuration *interface;

  if (configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  interface =
      &configuration->interface[NPU_WIFI_MT7996_RRO_INFORMATION_INTERFACE];
  if ((interface->valid_fields & NPU_WIFI_VALID_TX_RING_PCIE_ADDRESS) == 0U ||
      interface->tx_ring_pcie_address == 0U ||
      (interface->tx_ring_pcie_address & (sizeof(uint32_t) - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;

  an7581_mmio_write32(interface->tx_ring_pcie_address, cursor_value);
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result rro_delay(void *context, uint32_t duration) {
  const uint32_t *timer_clock_mhz = context;

  return timer_clock_mhz != NULL &&
                 an7581_local_timer_delay_ms(duration, *timer_clock_mhz)
             ? NPU_RUNTIME_SUCCESS
             : NPU_RUNTIME_IO_ERROR;
}

static bool board_map_host_buffer(void *context, uint32_t host_address,
                                  size_t length, uint32_t alignment,
                                  void **mapped_memory) {
  uint32_t local_address;

  (void)context;
  if (mapped_memory == NULL || length == 0U || length > UINT32_MAX ||
      !an7581_dma_buffer_map(host_address, (uint32_t)length, alignment,
                             &local_address))
    return false;

  *mapped_memory = (void *)(uintptr_t)local_address;
  return true;
}

static struct npu_wifi_mt7996_memory_binding
memory_binding_from_region(const struct npu_wifi_region *region, size_t size) {
  return (struct npu_wifi_mt7996_memory_binding){
      .memory = (void *)(uintptr_t)region->address,
      .size = size,
      .physical_base = region->address,
  };
}

static enum npu_runtime_result
configure_rro_board(struct npu_wifi_sram_allocator *allocator) {
  struct npu_wifi_region cpu_queue_region;
  struct npu_wifi_region dynamic_region;
  struct npu_wifi_region eagle_rx_counter_region[3];
  struct npu_wifi_region force_reset_region;
  struct npu_wifi_region icv_region;
  struct npu_wifi_region indication_region;
  struct npu_wifi_region msdu_page_id_region;
  struct npu_wifi_region packet_recycle_region;
  struct npu_wifi_region token_region;
  struct npu_wifi_region tx_packet_descriptor_region[2];
  volatile struct npu_wifi_rro_cpu_queue_entry *cpu_queue;
  uint32_t index;

  if (allocator == NULL ||
      !npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_SRAM_PACKET_ID_RECYCLE,
                                     &packet_recycle_region) ||
      packet_recycle_region.usable_size !=
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT * sizeof(uint16_t) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_TOKEN_ID_RING, &token_region) ||
      token_region.usable_size !=
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT * sizeof(uint16_t) ||
      !npu_wifi_mt7996_region_lookup(allocator,
                                     NPU_WIFI_MT7996_SRAM_TDM_RESET_SCRATCH,
                                     &force_reset_region) ||
      force_reset_region.usable_size != UINT32_C(0x1000) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_DYNAMIC_ARENA, &dynamic_region) ||
      dynamic_region.usable_size != NPU_WIFI_MT7996_DYNAMIC_ARENA_SIZE ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND2,
          &eagle_rx_counter_region[2]) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_RRO_CPU_QUEUE, &cpu_queue_region) ||
      cpu_queue_region.usable_size <
          NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT *
              sizeof(struct npu_wifi_rro_cpu_queue_entry) ||
      !npu_wifi_mt7996_dynamic_region_lookup(
          dynamic_region.address, NPU_WIFI_MT7996_DYNAMIC_RRO_INDICATIONS,
          &indication_region) ||
      indication_region.usable_size <
          NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT *
              sizeof(struct npu_wifi_rro_indication_descriptor) ||
      !npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_ICV_ERROR_TABLE, &icv_region) ||
      !npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_MSDU_PAGE_ID_MAP, &msdu_page_id_region) ||
      !npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_BAND0,
          &tx_packet_descriptor_region[0]) ||
      !npu_wifi_mt7996_fixed_region_lookup(
          NPU_WIFI_MT7996_FIXED_TX_PACKET_DESCRIPTORS_SECONDARY,
          &tx_packet_descriptor_region[1]) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND0,
          &eagle_rx_counter_region[0]) ||
      !npu_wifi_mt7996_region_lookup(
          allocator, NPU_WIFI_MT7996_SRAM_EAGLE_RX_COUNTERS_BAND1,
          &eagle_rx_counter_region[1]))
    return NPU_RUNTIME_OUT_OF_RANGE;

  cpu_queue = (volatile struct npu_wifi_rro_cpu_queue_entry *)(uintptr_t)
                  cpu_queue_region.address;
  for (index = 0U; index < NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT; ++index)
    cpu_queue[index].type = NPU_WIFI_RRO_CPU_QUEUE_FREE_MARKER;
  for (index = 0U; index < 3U; ++index)
    (void)npu_memset((void *)(uintptr_t)eagle_rx_counter_region[index].address,
                     0U, eagle_rx_counter_region[index].usable_size);
  g_completion_platform.diagnostic_counters =
      (volatile struct npu_wifi_mt7996_band2_diagnostic_counters *)(uintptr_t)
          eagle_rx_counter_region[2]
              .address;
  g_band0_diagnostic_counters =
      (volatile struct npu_wifi_mt7996_band0_diagnostic_counters *)(uintptr_t)
          eagle_rx_counter_region[0]
              .address;
  g_band1_diagnostic_counters =
      (volatile struct npu_wifi_mt7996_band1_diagnostic_counters *)(uintptr_t)
          eagle_rx_counter_region[1]
              .address;
  g_completion_platform.band0_diagnostic_counters = g_band0_diagnostic_counters;
  g_completion_platform.band1_diagnostic_counters = g_band1_diagnostic_counters;
  for (index = 0U; index < NPU_WIFI_INTERFACE_COUNT; ++index) {
    uint32_t counter_region_index =
        index == NPU_WIFI_MT7996_DEBUG_COUNTER_BAND0_INTERFACE   ? 0U
        : index == NPU_WIFI_MT7996_DEBUG_COUNTER_BAND1_INTERFACE ? 1U
                                                                 : 2U;

    g_firmware_state.wifi.interface[index].debug_counter_address =
        eagle_rx_counter_region[counter_region_index].address &
        NPU_WIFI_PHYSICAL_ADDRESS_MASK;
  }

  rro_pipeline_config = (struct npu_wifi_mt7996_rro_pipeline_config){
      .memory =
          {
              .normal_groups = {g_rro_normal_groups,
                                sizeof(g_rro_normal_groups)},
              .icv_errors = {(void *)(uintptr_t)icv_region.address,
                             NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT *
                                 sizeof(uint32_t)},
              .indication_descriptors = {(void *)(uintptr_t)
                                             indication_region.address,
                                         indication_region.usable_size},
              .page_release_queue =
                  {(void *)(uintptr_t)msdu_page_id_region.address,
                   NPU_WIFI_RRO_PAGE_RELEASE_QUEUE_SIZE * sizeof(uint16_t)},
              .cpu_queue = {(void *)(uintptr_t)cpu_queue_region.address,
                            cpu_queue_region.usable_size},
              .result_target = (const volatile uint32_t *)(uintptr_t)
                  AN7581_WIFI_RRO_RESULT_TARGET_ADDRESS,
              .result_observed =
                  &g_ppe_result_lifecycle.bundle.tdma_delivery.delivery.band[0]
                       .producer,
              .allocator_activity = (const volatile uint32_t *)(uintptr_t)
                  AN7581_WIFI_RRO_ALLOCATOR_ACTIVITY_ADDRESS,
          },
      .tdma_delivery = &g_ppe_result_lifecycle.bundle.tdma_delivery.delivery,
      .indication_attempt_counter =
          &g_band1_diagnostic_counters->rro_indication_attempts,
      .indication_available_counter =
          &g_band1_diagnostic_counters->rro_indication_descriptors_available,
      .table_generation_mismatch_counter =
          &g_band1_diagnostic_counters->rro_table_generation_mismatches,
      .metadata_page_delay_counter =
          &g_band0_diagnostic_counters->rro_metadata_pages_delayed,
      .routed_record_counter = &g_band1_diagnostic_counters->rro_routed_records,
      .packet_queue_release_counter =
          &g_band1_diagnostic_counters->rro_packet_queue_releases,
      .metadata_page_release_counter =
          &g_completion_platform.diagnostic_counters
               ->rro_metadata_pages_released,
      .cpu_queue_diagnostic_counters =
          {
              .entries_enqueued =
                  &g_band0_diagnostic_counters->rro_cpu_queue_entries_enqueued,
              .full_waits =
                  &g_band0_diagnostic_counters->rro_cpu_queue_full_waits,
              .entries_processed =
                  &g_band0_diagnostic_counters->rro_cpu_queue_entries_processed,
              .normal_entries =
                  &g_band0_diagnostic_counters->rro_cpu_queue_normal_entries,
          },
      .map_table = rro_map_table,
      .discard_cache = rro_discard_cache,
      .publish_cursor = rro_publish_cursor,
      .write32 = board_mmio_write32,
      .delay = rro_delay,
      .cursor_context = &g_firmware_state.wifi,
      .delay_context = &g_system_clock_rates.timer_mhz,
      .page_count = AN7581_WIFI_MT7996_RRO_PAGE_COUNT,
      .item_budget = NPU_WIFI_RRO_INDICATION_ITEM_COUNT_MASK,
      .record_budget = NPU_WIFI_RRO_METADATA_RECORD_COUNT_LIMIT,
      .cpu_queue_budget = NPU_WIFI_RRO_CPU_QUEUE_ENTRY_COUNT,
      /* The vendor worker retires one indication descriptor per iteration. */
      .indication_budget = 1U,
      /* Match the bounded reset wait recovered from the vendor path. */
      .reset_poll_limit = UINT32_C(1024),
      .information_interface = NPU_WIFI_MT7996_RRO_INFORMATION_INTERFACE,
      .normal_cpu_queue_enabled = true,
  };
  rro_control_plane_config = (struct npu_wifi_mt7996_control_plane_config){
      .shared_allocator = &g_completion_platform.allocator,
      .shared_packet_pool = &g_completion_platform.packet_pool,
      .packet_recycle = memory_binding_from_region(
          &packet_recycle_region,
          NPU_WIFI_PACKET_ID_RELEASE_ENTRY_COUNT * sizeof(uint16_t)),
      .token_ids = memory_binding_from_region(
          &token_region,
          NPU_WIFI_PACKET_ID_ALLOCATION_ENTRY_LIMIT * sizeof(uint16_t)),
      .force_reset_ids =
          memory_binding_from_region(&force_reset_region, UINT32_C(0x1000)),
      .dynamic_arena = memory_binding_from_region(
          &dynamic_region, NPU_WIFI_MT7996_DYNAMIC_ARENA_SIZE),
      .msdu_page_ids = memory_binding_from_region(
          &msdu_page_id_region,
          NPU_WIFI_BUFFER_ID_MAP_ENTRY_COUNT * sizeof(uint16_t)),
      .tx_done_packet_ids =
          {(void *)(uintptr_t)NPU_WIFI_TX_DONE_MT7996_PACKET_ID_MAP_ADDRESS,
           NPU_WIFI_RX_TX_DONE_DESCRIPTOR_LIMIT * sizeof(uint16_t),
           NPU_WIFI_TX_DONE_MT7996_PACKET_ID_MAP_ADDRESS},
      .diagnostic_counters = g_completion_platform.diagnostic_counters,
      .tx_packet_descriptors =
          {
              memory_binding_from_region(
                  &tx_packet_descriptor_region[0],
                  NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT *
                      NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE),
              memory_binding_from_region(
                  &tx_packet_descriptor_region[1],
                  NPU_WIFI_TX_PACKET_DESCRIPTOR_COUNT *
                      NPU_WIFI_TX_PACKET_DESCRIPTOR_SIZE),
          },
      .acquire = an7581_hardware_mutex_acquire,
      .release = an7581_hardware_mutex_release,
      .lock_context = &g_completion_platform.packet_pool_mutexes,
      .map_host_buffer = board_map_host_buffer,
      .tdm_rx_platform = &g_tx_fast_path_platform.fast_path.tdm_rx,
      .write32 = board_mmio_write32,
      .read32 = board_mmio_read32,
      .activation_allowed = true,
  };
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result prepare_rro_pipeline(void) {
  struct npu_wifi_mt7996_rro_pipeline_config *config =
      &g_rro_control_platform.pipeline_config;
  const struct npu_wifi_configuration *wifi = &g_firmware_state.wifi;
  const struct npu_wifi_interface_configuration *indication =
      &wifi->interface[NPU_WIFI_MT7996_RX_RRO_INDICATION_INTERFACE];
  uint32_t packet_mapping;
  uint32_t packet_span =
      AN7581_WIFI_MT7996_RRO_PACKET_COUNT * NPU_WIFI_RRO_PACKET_BUFFER_STRIDE;
  uint32_t page_mapping;
  uint32_t page_span =
      AN7581_WIFI_MT7996_RRO_PAGE_COUNT * NPU_WIFI_RRO_METADATA_PAGE_SIZE;

  if (!g_rro_control_platform.initialized ||
      !g_completion_platform.packet_pool.initialized ||
      !g_tx_fast_path_platform.fast_path.tdm_rx.initialized ||
      !g_ppe_result_lifecycle.bundle.tdma_delivery.delivery.initialized)
    return NPU_RUNTIME_EMPTY;
  if (!wifi->packet_buffer_address_valid || !wifi->dram_ba_node_address_valid ||
      (indication->valid_fields &
       (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT)) !=
          (NPU_WIFI_VALID_PCIE_ADDRESS | NPU_WIFI_VALID_DESCRIPTOR_COUNT))
    return NPU_RUNTIME_EMPTY;
  if (indication->descriptor_count !=
          NPU_WIFI_RRO_INDICATION_DESCRIPTOR_COUNT ||
      indication->pcie_address == 0U ||
      (indication->pcie_address & (sizeof(uint32_t) - 1U)) != 0U ||
      !an7581_dma_buffer_map(wifi->packet_buffer_address, packet_span,
                             NPU_WIFI_RRO_PACKET_BUFFER_STRIDE,
                             &packet_mapping) ||
      !an7581_dma_buffer_map(wifi->dram_ba_node_address, page_span,
                             NPU_WIFI_RRO_METADATA_PAGE_SIZE, &page_mapping))
    return NPU_RUNTIME_OUT_OF_RANGE;

  config->memory.metadata_records = (struct npu_wifi_rro_memory_binding){
      (void *)(uintptr_t)((wifi->dram_ba_node_address &
                           AN7581_DMA_PHYSICAL_MASK) |
                          NPU_WIFI_RRO_RECORD_ALIAS_BIT),
      page_span};
  config->memory.metadata_trailers = (struct npu_wifi_rro_memory_binding){
      (void *)(uintptr_t)page_mapping, page_span};
  config->memory.packet_buffers = (struct npu_wifi_rro_memory_binding){
      (void *)(uintptr_t)packet_mapping, packet_span};
  config->page_pool_base = wifi->dram_ba_node_address;
  config->packet_buffer_count = AN7581_WIFI_MT7996_RRO_PACKET_COUNT;
  config->indication_register_base = indication->pcie_address;
  return NPU_RUNTIME_SUCCESS;
}

static void lifecycle_mailbox_observer(void *context, uint32_t outer_function,
                                       bool request_applied) {
  struct an7581_wifi_mt7996_rro_control_lifecycle_result rro_control_result;
  struct an7581_wifi_mt7996_rx_refill_lifecycle_result rx_refill_result;
  struct an7581_wifi_mt7996_tx_fast_path_lifecycle_result tx_fast_path_result;
  struct an7581_wifi_mt7996_completion_lifecycle_result completion_result;
  struct an7581_wifi_mt7996_ppe_result_lifecycle_result ppe_result;
  struct an7581_tr471_runtime_lifecycle_result tr471_result;

  (void)context;
  if (!request_applied)
    return;

  if (outer_function == NPU_OUTER_FUNCTION_WIFI)
    wifi_data_plane_handle_request();
  if (outer_function == NPU_OUTER_FUNCTION_WIFI &&
      g_wifi_data_plane_transition == WIFI_DATA_PLANE_RUNNING &&
      g_completion_lifecycle.initialized)
    (void)an7581_wifi_mt7996_completion_lifecycle_step(&g_completion_lifecycle,
                                                       &completion_result);
  if (outer_function == NPU_OUTER_FUNCTION_WIFI &&
      g_wifi_data_plane_transition == WIFI_DATA_PLANE_RUNNING &&
      g_tx_fast_path_lifecycle.initialized)
    (void)an7581_wifi_mt7996_tx_fast_path_lifecycle_step(
        &g_tx_fast_path_lifecycle, &tx_fast_path_result);
  if (outer_function == NPU_OUTER_FUNCTION_WIFI &&
      g_wifi_data_plane_transition == WIFI_DATA_PLANE_RUNNING &&
      g_ppe_result_lifecycle.initialized)
    (void)an7581_wifi_mt7996_ppe_result_lifecycle_step(&g_ppe_result_lifecycle,
                                                       &ppe_result);
  if (outer_function == NPU_OUTER_FUNCTION_WIFI &&
      g_wifi_data_plane_transition == WIFI_DATA_PLANE_RUNNING &&
      g_rro_control_lifecycle.initialized) {
    enum npu_runtime_result prepare_status = prepare_rro_pipeline();

    if (!g_rro_control_lifecycle.control_plane_initialized ||
        prepare_status == NPU_RUNTIME_SUCCESS)
      (void)an7581_wifi_mt7996_rro_control_lifecycle_step(
          &g_rro_control_lifecycle, &rro_control_result);
  }
  if (outer_function == NPU_OUTER_FUNCTION_WIFI &&
      g_wifi_data_plane_transition == WIFI_DATA_PLANE_RUNNING &&
      g_rx_refill_lifecycle.initialized)
    (void)an7581_wifi_mt7996_rx_refill_lifecycle_step(&g_rx_refill_lifecycle,
                                                      &rx_refill_result);
  if (outer_function == NPU_OUTER_FUNCTION_WIFI) {
    wifi_data_plane_transition_progress();
    publish_wifi_runtime_readiness();
  }
  if (outer_function == NPU_OUTER_FUNCTION_TUNNEL &&
      g_tunnel_platform.initialized)
    (void)an7581_tunnel_platform_synchronize_mailbox_state(&g_tunnel_platform);
  if (outer_function == NPU_OUTER_FUNCTION_TR471 &&
      g_tr471_runtime_lifecycle.initialized)
    (void)an7581_tr471_runtime_lifecycle_step(&g_tr471_runtime_lifecycle,
                                              &tr471_result);
}

static void core4_dispatch_loop(void) {
  for (;;) {
    struct an7581_core4_dispatch_result result;

    (void)an7581_core4_dispatch_step(&g_core4_dispatch, AN7581_CORE4_HART,
                                     &result);
    if (result.waiting_for_worker || result.should_backoff)
      an7581_cpu_relax();
  }
}

static void core1_dispatch_loop(void) {
  for (;;) {
    struct an7581_core1_dispatch_result result;

    (void)an7581_core1_dispatch_step(&g_core1_dispatch, AN7581_CORE1_HART,
                                     &result);
    if (result.waiting_for_worker || result.should_backoff)
      an7581_cpu_relax();
  }
}

static void completion_packet_queue_dispatch_loop(void) {
  for (;;) {
    struct an7581_wifi_mt7996_completion_dispatch_result result;

    (void)an7581_wifi_mt7996_completion_dispatch_step(
        &g_completion_dispatch, AN7581_WIFI_MT7996_COMPLETION_PACKET_QUEUE_HART,
        &result);
    if (result.waiting_for_pipeline || result.should_backoff)
      an7581_cpu_relax();
  }
}

static void core56_dispatch_loop(uint32_t core) {
  for (;;) {
    struct an7581_core56_dispatch_result result;

    (void)an7581_core56_dispatch_step(&g_core56_dispatch, core, &result);
    if (result.waiting_for_runtime || result.should_backoff)
      an7581_cpu_relax();
  }
}

static void firmware_core5_main(void) {
  core56_dispatch_loop(AN7581_CORE5_HART);
}

static void firmware_core6_main(void) {
  core56_dispatch_loop(AN7581_CORE6_HART);
}

static void firmware_core2_main(void) {
  for (;;) {
    struct an7581_core2_dispatch_result result;

    (void)an7581_core2_dispatch_step(&g_core2_dispatch, AN7581_CORE2_HART,
                                     &result);
    if (result.waiting_for_worker || result.should_backoff)
      an7581_cpu_relax();
  }
}

static void firmware_core7_main(void) {
  while (!g_platform_interrupts_ready)
    an7581_cpu_relax();
  an7581_memory_barrier();
  npu_wifi_core7_init_wrapper(&g_firmware_state.wifi.compatibility_diagnostics);
  if (!an7581_mailbox_interrupt_initialize(&g_mailbox_runtime,
                                           AN7581_CORE7_HART))
    an7581_panic(AN7581_PANIC_MAILBOX_RUNTIME);
  if (an7581_tunnel_platform_mt7996_initialize(
          &g_tunnel_platform, g_completion_platform.allocator_owner,
          &g_firmware_state.tunnel,
          g_system_clock_rates.timer_mhz) != NPU_RUNTIME_SUCCESS ||
      an7581_tunnel_platform_synchronize_mailbox_state(&g_tunnel_platform) !=
          NPU_RUNTIME_SUCCESS ||
      an7581_core7_dispatch_publish_tunnel(
          &g_core7_dispatch, &g_tunnel_platform) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  an7581_mailbox_poll(&g_mailbox_runtime, AN7581_CORE7_HART);
  an7581_enable_machine_external_interrupts();

  for (;;) {
    struct an7581_core7_dispatch_result result;

    (void)an7581_core7_dispatch_step(&g_core7_dispatch, AN7581_CORE7_HART,
                                     &result);
    if (result.waiting_for_runtime || result.should_backoff)
      an7581_cpu_relax();
  }
}

static void tr471_runtime_lifecycle_progress(void) {
  struct an7581_tr471_runtime_lifecycle_result result;

  if (g_tr471_runtime_lifecycle.initialized &&
      g_tr471_runtime_lifecycle.activation_allowed)
    (void)an7581_tr471_runtime_lifecycle_step(&g_tr471_runtime_lifecycle,
                                              &result);
}

void firmware_main(uint32_t core) {
  /*
   * The MT7996 image writes this sentinel to MIB 31 after each hart completes
   * its common boot sequence.  No public register documentation assigns a
   * narrower field-level meaning to the value.
   */
  const uint32_t common_boot_completion_sentinel = UINT32_C(0xcccccccc);
  const uint32_t sram_allocator_mutex_handle = UINT32_C(0x12);
  struct an7581_mailbox_host_notification_config host_notification_config;
  struct an7581_wifi_mt7996_completion_board_configuration
      completion_board_configuration = {0};
  struct an7581_wifi_mt7996_completion_platform_config platform_config = {0};
  struct an7581_wifi_mt7996_completion_lifecycle_config lifecycle_config;
  struct an7581_wifi_mt7996_ppe_result_board_configuration
      ppe_result_board_configuration = {0};
  struct an7581_wifi_mt7996_ppe_result_lifecycle_config
      ppe_result_lifecycle_config;
  struct an7581_wifi_mt7996_rro_control_board_configuration
      rro_control_board_configuration = {0};
  struct an7581_wifi_mt7996_rro_control_lifecycle_config
      rro_control_lifecycle_config;
  struct an7581_wifi_mt7996_rx_refill_board_configuration
      rx_refill_board_configuration = {0};
  struct an7581_wifi_mt7996_rx_refill_lifecycle_config
      rx_refill_lifecycle_config;
  struct an7581_wifi_mt7996_tx_fast_path_board_configuration
      tx_fast_path_board_configuration = {0};
  struct an7581_wifi_mt7996_tx_fast_path_lifecycle_config
      tx_fast_path_lifecycle_config;
  struct an7581_wifi_mt7996_data_plane_stop_config data_plane_stop_config;
  struct an7581_tr471_lifecycle_config tr471_lifecycle_config;
  struct an7581_tr471_board_configuration tr471_board_configuration = {0};
  struct an7581_tr471_runtime_platform_config tr471_runtime_platform_config;
  struct an7581_tr471_runtime_lifecycle_config tr471_runtime_lifecycle_config;
  struct npu_tr471_runtime_io *tr471_runtime = NULL;
  bool completion_lifecycle_ready;
  bool ppe_result_lifecycle_ready;
  bool rro_control_lifecycle_ready;
  bool rx_refill_lifecycle_ready;
  bool tx_fast_path_lifecycle_ready;
  bool data_plane_stop_ready;
  bool tr471_lifecycle_ready;
  bool tr471_runtime_lifecycle_ready;

  if (core != 0U) {
    while (!g_platform_interrupts_ready)
      an7581_cpu_relax();
    an7581_memory_barrier();
    an7581_plic_initialize(core);
    an7581_mmio_write32(AN7581_NPU_MIB_31, common_boot_completion_sentinel);
    if (core != AN7581_TR471_RUNTIME_HART)
      an7581_enable_machine_external_interrupts();

    if (core == AN7581_WIFI_MT7996_COMPLETION_PACKET_QUEUE_HART)
      completion_packet_queue_dispatch_loop();
    if (core == AN7581_CORE1_HART)
      core1_dispatch_loop();
    if (core == AN7581_CORE4_HART)
      core4_dispatch_loop();
    if (core == AN7581_TR471_TIMER_HART)
      firmware_core2_main();
    if (core == AN7581_CORE5_HART)
      firmware_core5_main();
    if (core == AN7581_CORE6_HART)
      firmware_core6_main();
    if (core == AN7581_TR471_RUNTIME_HART)
      firmware_core7_main();

    for (;;)
      an7581_wait_for_interrupt();
  }

  an7581_plic_initialize(core);
  if (!an7581_system_initialize(&g_system_clock_rates))
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  tr471_board_binding.timer_clock_mhz = g_system_clock_rates.timer_mhz;
  host_notification_config = (struct an7581_mailbox_host_notification_config){
      .attempts = UINT32_C(0x1e),
      .timer_clock_mhz = g_system_clock_rates.timer_mhz,
      .enabled = true,
  };
  if (an7581_core1_dispatch_initialize(&g_core1_dispatch) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_core2_dispatch_initialize(
          &g_core2_dispatch, &g_tr471_runtime_dispatch) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_core7_dispatch_initialize(
          &g_core7_dispatch, &g_tr471_runtime_dispatch) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);

  an7581_shared_sram_reset();
  an7581_l2_cache_initialize();
  npu_firmware_state_reset(&g_firmware_state);
  if (an7581_wifi_mt7996_runtime_readiness_initialize(&g_wifi_readiness) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_data_image_decode(
          (const uint8_t *)(uintptr_t)AN7581_NPU_LOCAL_DATA_BASE,
          AN7581_DATA_IMAGE_CURRENT_SIZE,
          &g_data_configuration) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_DATA_IMAGE);
  (void)an7581_data_configuration_apply_tunnel(&g_data_configuration,
                                               &g_firmware_state.tunnel);
  if (an7581_tr471_board_binding_resolve(&tr471_board_binding,
                                         &tr471_board_configuration) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_wifi_mt7996_completion_board_binding_resolve(
          &completion_board_binding, &completion_board_configuration) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_wifi_mt7996_ppe_result_board_binding_resolve(
          &ppe_result_board_binding, &ppe_result_board_configuration) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  completion_lifecycle_ready = true;
  tr471_lifecycle_ready = true;
  if (completion_lifecycle_ready)
    platform_config = (struct an7581_wifi_mt7996_completion_platform_config){
        .dispatch = &g_completion_dispatch,
        .readiness = &g_wifi_readiness,
        .wake_workers = completion_board_configuration.wake_workers,
        .wake_context = completion_board_configuration.wake_context,
        .read_hart_id = an7581_hardware_mutex_read_current_hart,
        .vdma_poll_limit = completion_board_configuration.vdma_poll_limit,
        .tx_done_budget = completion_board_configuration.tx_done_budget,
        .band0_budget = completion_board_configuration.band0_budget,
        .packet_queue_producer =
            completion_board_configuration.packet_queue_producer,
        .packet_queue_consumers =
            {
                completion_board_configuration.packet_queue_consumers[0],
                completion_board_configuration.packet_queue_consumers[1],
            },
    };
  if (completion_lifecycle_ready)
    completion_lifecycle_ready =
        an7581_wifi_mt7996_completion_platform_initialize(
            &g_completion_platform, &platform_config) == NPU_RUNTIME_SUCCESS;
  if (completion_lifecycle_ready)
    completion_lifecycle_ready =
        an7581_hardware_mutex_shared_bank_initialize(
            &g_sram_allocator_mutex, an7581_hardware_mutex_read_current_hart,
            NULL, &sram_allocator_mutex_handle, 1U) == NPU_RUNTIME_SUCCESS;
  if (completion_lifecycle_ready)
    completion_lifecycle_ready =
        npu_wifi_sram_allocator_configure_lock(
            g_completion_platform.allocator_owner,
            an7581_hardware_mutex_acquire, an7581_hardware_mutex_release,
            &g_sram_allocator_mutex, 0U) == NPU_RUNTIME_SUCCESS;
  if (!completion_lifecycle_ready ||
      configure_rro_board(g_completion_platform.allocator_owner) !=
          NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_wifi_mt7996_rro_control_board_binding_resolve(
          &rro_control_board_binding, &g_core56_dispatch,
          &g_rro_control_platform,
          &rro_control_board_configuration) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_wifi_mt7996_rx_refill_board_binding_resolve(
          &rx_refill_board_binding, &rx_refill_board_configuration) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (an7581_wifi_mt7996_tx_fast_path_board_binding_resolve(
          &tx_fast_path_board_binding, &g_core2_dispatch,
          &g_completion_platform.allocator, &g_completion_platform.packet_pool,
          &g_wifi_readiness, g_band0_diagnostic_counters,
          g_band1_diagnostic_counters, &g_tx_fast_path_platform,
          &tx_fast_path_board_configuration) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  rro_control_lifecycle_ready = completion_lifecycle_ready;
  rx_refill_lifecycle_ready = completion_lifecycle_ready;
  tx_fast_path_lifecycle_ready = completion_lifecycle_ready;
  rro_control_lifecycle_config =
      (struct an7581_wifi_mt7996_rro_control_lifecycle_config){
          .configuration = &g_firmware_state.wifi,
          .platform = rro_control_board_configuration.platform,
          .activation_allowed =
              rro_control_board_configuration.activation_allowed,
      };
  if (rro_control_lifecycle_ready)
    rro_control_lifecycle_ready =
        an7581_wifi_mt7996_rro_control_lifecycle_initialize(
            &g_rro_control_lifecycle, &rro_control_lifecycle_config) ==
        NPU_RUNTIME_SUCCESS;
  rx_refill_lifecycle_config =
      (struct an7581_wifi_mt7996_rx_refill_lifecycle_config){
          .configuration = &g_firmware_state.wifi,
          .control_lifecycle = &g_rro_control_lifecycle,
          .dispatch = &g_core1_dispatch,
          .operations = rx_refill_board_configuration.operations,
          .diagnostic_counters =
              {
                  {
                      &g_band0_diagnostic_counters->rx_packet_id_allocations,
                      &g_band0_diagnostic_counters
                           ->rx_packet_id_allocation_failures,
                  },
                  {
                      &g_band1_diagnostic_counters->rx_packet_id_allocations,
                      &g_band1_diagnostic_counters
                           ->rx_packet_id_allocation_failures,
                  },
                  {&g_band0_diagnostic_counters->msdu_page_refills_band0, NULL},
                  {&g_band0_diagnostic_counters->msdu_page_refills_band1, NULL},
                  {&g_band0_diagnostic_counters->msdu_page_refills_band2, NULL},
              },
          .operation_context = rx_refill_board_configuration.operation_context,
          .activation_allowed =
              rx_refill_board_configuration.activation_allowed,
      };
  rx_refill_lifecycle_ready =
      rx_refill_lifecycle_ready && rro_control_lifecycle_ready;
  if (rx_refill_lifecycle_ready)
    rx_refill_lifecycle_ready =
        an7581_wifi_mt7996_rx_refill_lifecycle_initialize(
            &g_rx_refill_lifecycle, &rx_refill_lifecycle_config) ==
        NPU_RUNTIME_SUCCESS;
  lifecycle_config = (struct an7581_wifi_mt7996_completion_lifecycle_config){
      .configuration = &g_firmware_state.wifi,
      .operations = an7581_wifi_mt7996_completion_platform_operations(),
      .operation_context = &g_completion_platform,
      .activation_allowed = completion_board_configuration.activation_allowed,
  };
  if (completion_lifecycle_ready)
    completion_lifecycle_ready =
        an7581_wifi_mt7996_completion_lifecycle_initialize(
            &g_completion_lifecycle, &lifecycle_config) == NPU_RUNTIME_SUCCESS;
  tx_fast_path_lifecycle_ready =
      tx_fast_path_lifecycle_ready && completion_lifecycle_ready;
  tx_fast_path_lifecycle_config =
      (struct an7581_wifi_mt7996_tx_fast_path_lifecycle_config){
          .configuration = &g_firmware_state.wifi,
          .operations =
              tx_fast_path_board_configuration.platform != NULL
                  ? an7581_wifi_mt7996_tx_fast_path_platform_operations()
                  : NULL,
          .operation_context = tx_fast_path_board_configuration.platform,
          .activation_allowed =
              tx_fast_path_board_configuration.activation_allowed,
      };
  if (tx_fast_path_lifecycle_ready)
    tx_fast_path_lifecycle_ready =
        an7581_wifi_mt7996_tx_fast_path_lifecycle_initialize(
            &g_tx_fast_path_lifecycle, &tx_fast_path_lifecycle_config) ==
        NPU_RUNTIME_SUCCESS;
  ppe_result_lifecycle_ready = completion_lifecycle_ready;
  ppe_result_lifecycle_config =
      (struct an7581_wifi_mt7996_ppe_result_lifecycle_config){
          .configuration = &g_firmware_state.wifi,
          .packet_pool = g_completion_platform.packet_pool_owner,
          .completion_lifecycle = &g_completion_lifecycle,
          .memory = ppe_result_board_configuration.memory,
          .diagnostic_counters = g_band1_diagnostic_counters,
          .tdma_diagnostic_counters = g_band0_diagnostic_counters,
          .hart_id = ppe_result_board_configuration.hart_id,
          .packet_queue_producer =
              ppe_result_board_configuration.packet_queue_producer,
          .fragment_queue_producer =
              ppe_result_board_configuration.fragment_queue_producer,
          .activation_allowed =
              ppe_result_board_configuration.activation_allowed,
      };
  if (ppe_result_lifecycle_ready)
    ppe_result_lifecycle_ready =
        an7581_wifi_mt7996_ppe_result_lifecycle_initialize(
            &g_ppe_result_lifecycle, &ppe_result_lifecycle_config) ==
        NPU_RUNTIME_SUCCESS;
  data_plane_stop_ready =
      rro_control_lifecycle_ready && rx_refill_lifecycle_ready &&
      completion_lifecycle_ready && tx_fast_path_lifecycle_ready &&
      ppe_result_lifecycle_ready;
  data_plane_stop_config = (struct an7581_wifi_mt7996_data_plane_stop_config){
      .rro_control = &g_rro_control_lifecycle,
      .rx_refill = &g_rx_refill_lifecycle,
      .completion = &g_completion_lifecycle,
      .tx_fast_path = &g_tx_fast_path_lifecycle,
      .ppe_result = &g_ppe_result_lifecycle,
      .completion_platform = &g_completion_platform,
      .tx_fast_path_platform = &g_tx_fast_path_platform,
      .core2_dispatch = &g_core2_dispatch,
      .core56_dispatch = &g_core56_dispatch,
      .completion_dispatch = &g_completion_dispatch,
      .rro_control_board = &rro_control_board_configuration,
      .rx_refill_board = &rx_refill_board_configuration,
      .completion_board = &completion_board_configuration,
      .tx_fast_path_board = &tx_fast_path_board_configuration,
      .ppe_result_board = &ppe_result_board_configuration,
  };
  if (data_plane_stop_ready &&
      an7581_wifi_mt7996_data_plane_stop_initialize(
          &g_data_plane_stop, &data_plane_stop_config) != NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_CONFIGURATION);
  if (tr471_lifecycle_ready) {
    tr471_lifecycle_ready = an7581_tr471_lifecycle_platform_initialize(
                                &g_tr471_platform) == NPU_RUNTIME_SUCCESS;
    tr471_lifecycle_config = (struct an7581_tr471_lifecycle_config){
        .state = &g_firmware_state.tr471,
        .operations = an7581_tr471_lifecycle_platform_operations(),
        .operation_context = &g_tr471_platform,
        .shared_buffer_extent = tr471_board_configuration.shared_buffer_extent,
        .activation_allowed = tr471_board_configuration.activation_allowed,
    };
    tr471_runtime = &g_tr471_platform.runtime;
  }
  if (tr471_lifecycle_ready)
    tr471_lifecycle_ready =
        an7581_tr471_lifecycle_initialize(
            &g_tr471_lifecycle, &tr471_lifecycle_config) == NPU_RUNTIME_SUCCESS;
  tr471_runtime_lifecycle_ready = tr471_lifecycle_ready;
  if (tr471_runtime_lifecycle_ready &&
      tr471_board_configuration.activation_allowed) {
    tr471_runtime_platform_config =
        (struct an7581_tr471_runtime_platform_config){
            .service_lifecycle = &g_tr471_lifecycle,
            .state = &g_firmware_state.tr471,
            .runtime = tr471_runtime,
            .dispatch = &g_tr471_runtime_dispatch,
            .wake_harts = tr471_board_configuration.wake_harts,
            .wake_context = tr471_board_configuration.wake_context,
            .timer_clock_mhz = tr471_board_configuration.timer_clock_mhz,
            .transmit_budget = tr471_board_configuration.transmit_budget,
            .receive_budget = tr471_board_configuration.receive_budget,
        };
    tr471_runtime_lifecycle_ready =
        an7581_tr471_runtime_platform_initialize(
            &g_tr471_runtime_platform, &tr471_runtime_platform_config) ==
        NPU_RUNTIME_SUCCESS;
  }
  tr471_runtime_lifecycle_config =
      (struct an7581_tr471_runtime_lifecycle_config){
          .operations = tr471_board_configuration.activation_allowed
                            ? an7581_tr471_runtime_platform_operations()
                            : NULL,
          .operation_context = tr471_board_configuration.activation_allowed
                                   ? &g_tr471_runtime_platform
                                   : NULL,
          .activation_allowed = tr471_board_configuration.activation_allowed,
      };
  if (tr471_runtime_lifecycle_ready)
    tr471_runtime_lifecycle_ready =
        an7581_tr471_runtime_lifecycle_initialize(
            &g_tr471_runtime_lifecycle, &tr471_runtime_lifecycle_config) ==
        NPU_RUNTIME_SUCCESS;
  if (!an7581_ppe_runtime_reset(&g_ppe_runtime))
    an7581_panic(AN7581_PANIC_PPE_RUNTIME);
  npu_ppe_state_set_backend(&g_firmware_state.ppe,
                            an7581_ppe_backend_operations(), &g_ppe_runtime);
  if (an7581_mailbox_runtime_reset(&g_mailbox_runtime, &g_firmware_state,
                                   &host_notification_config) !=
      NPU_RUNTIME_SUCCESS)
    an7581_panic(AN7581_PANIC_MAILBOX_RUNTIME);
  if (!an7581_mailbox_set_dispatch_observer(&g_mailbox_runtime,
                                            lifecycle_mailbox_observer, NULL))
    an7581_panic(AN7581_PANIC_MAILBOX_RUNTIME);
  if (!an7581_mailbox_interrupt_initialize(&g_mailbox_runtime, core))
    an7581_panic(AN7581_PANIC_MAILBOX_RUNTIME);
  an7581_mmio_write32(AN7581_NPU_MIB_31, common_boot_completion_sentinel);
  an7581_memory_barrier();
  g_platform_interrupts_ready = true;
  an7581_memory_barrier();
  an7581_mailbox_poll(&g_mailbox_runtime, core);
  an7581_enable_machine_external_interrupts();

  for (;;) {
    tr471_runtime_lifecycle_progress();
    wifi_data_plane_transition_progress();
    if (wifi_data_plane_transition_pending())
      an7581_cpu_relax();
    else
      an7581_wait_for_interrupt();
  }
}

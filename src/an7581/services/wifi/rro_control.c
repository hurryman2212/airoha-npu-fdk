/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_control.h"

#include "an7581/platform/dma.h"
#include "an7581/runtime/memory.h"

#define NPU_WIFI_RRO_PHYSICAL_MASK UINT32_C(0x3fffffff)
#define NPU_WIFI_RRO_PHYSICAL_LIMIT UINT32_C(0x40000000)
#define NPU_WIFI_RRO_NORMAL_TABLE_SIZE                                         \
  (NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT *                                     \
   (uint32_t)sizeof(struct npu_wifi_rro_metadata_table_entry))
#define NPU_WIFI_RRO_SPECIAL_TABLE_SIZE                                        \
  (NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT *                                    \
   (uint32_t)sizeof(struct npu_wifi_rro_metadata_table_entry))

static bool
table_backend_valid(const struct npu_wifi_rro_table_backend *backend) {
  return backend != NULL && backend->normal_groups != NULL &&
         backend->normal_group_count == NPU_WIFI_RRO_NORMAL_TABLE_GROUP_LIMIT &&
         backend->normal_entry_count == NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT &&
         backend->special_entry_count == NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT;
}

enum npu_runtime_result npu_wifi_rro_control_initialize(
    struct npu_wifi_rro_control *control,
    const struct npu_wifi_rro_control_config *config) {
  if (control == NULL || config == NULL ||
      !table_backend_valid(config->table_backend) ||
      config->icv_error_table == NULL || config->map_table == NULL ||
      config->reset_buffers == NULL || config->set_page_pool_address == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (((uintptr_t)config->icv_error_table & (sizeof(uint32_t) - 1U)) != 0U ||
      config->icv_error_word_count < NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(control, 0U, sizeof(*control));
  control->table_backend = config->table_backend;
  control->icv_error_table = config->icv_error_table;
  control->map_table = config->map_table;
  control->reset_buffers = config->reset_buffers;
  control->set_page_pool_address = config->set_page_pool_address;
  control->map_context = config->map_context;
  control->reset_context = config->reset_context;
  control->page_pool_context = config->page_pool_context;
  control->icv_error_word_count = config->icv_error_word_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_control_bind_npu_information(
    struct npu_wifi_rro_control *control,
    struct npu_wifi_configuration *configuration, uint32_t interface) {
  if (control == NULL || configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (interface >= NPU_WIFI_INTERFACE_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (control->applied_action_count != 0U ||
      control->rejected_action_count != 0U)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (control->configuration != NULL &&
      (control->configuration != configuration ||
       control->information_interface != interface))
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  control->configuration = configuration;
  control->information_interface = interface;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_wifi_rro_control_bind_lifecycle(
    struct npu_wifi_rro_control *control,
    npu_wifi_rro_control_lifecycle_operation prepare_stop,
    npu_wifi_rro_control_lifecycle_operation resume, void *context) {
  if (control == NULL || prepare_stop == NULL || resume == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (control->applied_action_count != 0U ||
      control->rejected_action_count != 0U)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (control->prepare_stop != NULL &&
      (control->prepare_stop != prepare_stop || control->resume != resume ||
       control->lifecycle_context != context))
    return NPU_RUNTIME_OWNERSHIP_ERROR;

  control->prepare_stop = prepare_stop;
  control->resume = resume;
  control->lifecycle_context = context;
  return NPU_RUNTIME_SUCCESS;
}

static void publish_npu_information(struct npu_wifi_rro_control *control,
                                    uint32_t information) {
  if (control->configuration == NULL)
    return;

  control->configuration->npu_information.worker_status = information;
  control->configuration->npu_information.mt7996_fast_path_gate = 1U;
  control->configuration->npu_information.mt7996_tx_done_gate = 1U;
}

static enum npu_runtime_result map_and_prepare_table(
    struct npu_wifi_rro_control *control, uint32_t host_address,
    uint32_t table_size, uint32_t entry_count,
    volatile struct npu_wifi_rro_metadata_table_entry **entries) {
  uint32_t physical_address = host_address & NPU_WIFI_RRO_PHYSICAL_MASK;
  enum npu_runtime_result status;

  if (physical_address > NPU_WIFI_RRO_PHYSICAL_LIMIT - table_size)
    return NPU_RUNTIME_OUT_OF_RANGE;
  status = control->map_table(control->map_context, physical_address,
                              table_size, entries);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_wifi_rro_table_backend_prepare_entries(*entries, entry_count);
}

static enum npu_runtime_result
initialize_normal_table(struct npu_wifi_rro_control *control,
                        const struct npu_wifi_inode_registers *registers) {
  volatile struct npu_wifi_rro_metadata_table_entry *entries = NULL;
  uint32_t group = registers->direction;
  bool newly_configured;
  enum npu_runtime_result status;

  if (control->ring_enabled != 0U)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (group >= control->table_backend->normal_group_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  newly_configured = control->table_backend->normal_groups[group] == NULL;
  status = map_and_prepare_table(
      control, registers->input_count_address, NPU_WIFI_RRO_NORMAL_TABLE_SIZE,
      NPU_WIFI_RRO_NORMAL_TABLE_ENTRY_LIMIT, &entries);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  status = npu_wifi_rro_table_backend_set_normal_group(control->table_backend,
                                                       group, entries);
  if (status == NPU_RUNTIME_SUCCESS && newly_configured)
    ++control->configured_normal_group_count;
  return status;
}

static enum npu_runtime_result
initialize_special_table(struct npu_wifi_rro_control *control,
                         const struct npu_wifi_inode_registers *registers) {
  volatile struct npu_wifi_rro_metadata_table_entry *entries = NULL;
  enum npu_runtime_result status;

  if (control->ring_enabled != 0U)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  status = map_and_prepare_table(
      control, registers->input_count_address, NPU_WIFI_RRO_SPECIAL_TABLE_SIZE,
      NPU_WIFI_RRO_SPECIAL_TABLE_ENTRY_LIMIT, &entries);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;
  return npu_wifi_rro_table_backend_set_special_table(control->table_backend,
                                                      entries);
}

static enum npu_runtime_result
start_runtime(struct npu_wifi_rro_control *control) {
  enum npu_runtime_result status;
  uint32_t index;

  if (!control->page_pool_address_valid ||
      control->configured_normal_group_count == 0U ||
      control->table_backend->special_table == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (control->reset_required) {
    if (!control->reset_completed ||
        control->reset_generation != control->stop_generation)
      return NPU_RUNTIME_OWNERSHIP_ERROR;
    if (control->resume != NULL) {
      status = control->resume(control->lifecycle_context);
      if (status != NPU_RUNTIME_SUCCESS)
        return status;
    }
    ++control->resume_generation;
  }

  for (index = 0U; index < NPU_WIFI_RRO_ICV_ERROR_WORD_COUNT; ++index)
    control->icv_error_table[index] = 0U;
  control->rx_stopped = false;
  control->stop_prepared = false;
  control->reset_required = false;
  control->reset_completed = false;
  control->configuration_ready = 1U;
  an7581_dma_memory_barrier();
  control->ring_enabled = 1U;
  publish_npu_information(control, 1U);
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
stop_runtime(struct npu_wifi_rro_control *control) {
  control->ring_enabled = 0U;
  an7581_dma_memory_barrier();
  control->configuration_ready = 0U;
  an7581_dma_memory_barrier();
  control->rx_stopped = true;
  control->stop_prepared = control->prepare_stop == NULL;
  control->reset_required = true;
  control->reset_completed = false;
  ++control->stop_generation;
  publish_npu_information(control, 0U);
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_wifi_rro_control_finish_stop(struct npu_wifi_rro_control *control) {
  enum npu_runtime_result status;

  if (control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!control->rx_stopped || !control->reset_required)
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (control->stop_prepared)
    return NPU_RUNTIME_SUCCESS;
  if (control->prepare_stop == NULL)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = control->prepare_stop(control->lifecycle_context);
  if (status == NPU_RUNTIME_SUCCESS)
    control->stop_prepared = true;
  return status;
}

static enum npu_runtime_result
resume_runtime(struct npu_wifi_rro_control *control) {
  enum npu_runtime_result status;

  if (control->reset_required &&
      (!control->reset_completed ||
       control->reset_generation != control->stop_generation))
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  if (control->reset_required && control->resume != NULL) {
    status = control->resume(control->lifecycle_context);
    if (status != NPU_RUNTIME_SUCCESS)
      return status;
  }

  control->rx_stopped = false;
  control->stop_prepared = false;
  control->reset_required = false;
  ++control->resume_generation;
  control->configuration_ready = 1U;
  an7581_dma_memory_barrier();
  control->ring_enabled = 1U;
  publish_npu_information(control, 1U);
  an7581_dma_memory_barrier();
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
apply_action(struct npu_wifi_rro_control *control, uint32_t action,
             const struct npu_wifi_inode_registers *registers) {
  switch (action) {
  case NPU_WIFI_RRO_INODE_INITIALIZE_NORMAL_TABLE:
    return initialize_normal_table(control, registers);
  case NPU_WIFI_RRO_INODE_INITIALIZE_SPECIAL_TABLE:
    return initialize_special_table(control, registers);
  case NPU_WIFI_RRO_INODE_START:
    return start_runtime(control);
  case NPU_WIFI_RRO_INODE_INVALIDATE_SELECTOR:
    return npu_wifi_rro_table_backend_invalidate_selector(
        control->table_backend, registers->direction & UINT32_C(0xffff));
  case NPU_WIFI_RRO_INODE_STOP:
    return stop_runtime(control);
  case NPU_WIFI_RRO_INODE_UNSUPPORTED:
    ++control->unsupported_action_count;
    return NPU_RUNTIME_SUCCESS;
  case NPU_WIFI_RRO_INODE_RESET_BUFFER_IDS:
    if (!control->rx_stopped || !control->stop_prepared)
      return NPU_RUNTIME_OWNERSHIP_ERROR;
    {
      enum npu_runtime_result status =
          control->reset_buffers(control->reset_context);

      if (status != NPU_RUNTIME_SUCCESS)
        return status;
      control->reset_generation = control->stop_generation;
      control->reset_completed = true;
      return NPU_RUNTIME_SUCCESS;
    }
  case NPU_WIFI_RRO_INODE_RESUME:
    return resume_runtime(control);
  default:
    return NPU_RUNTIME_OUT_OF_RANGE;
  }
}

enum npu_runtime_result
npu_wifi_rro_control_apply(struct npu_wifi_rro_control *control,
                           uint32_t action,
                           const struct npu_wifi_inode_registers *registers) {
  enum npu_runtime_result status;

  if (control == NULL || registers == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!table_backend_valid(control->table_backend) ||
      control->icv_error_table == NULL || control->map_table == NULL ||
      control->reset_buffers == NULL ||
      control->icv_error_word_count < NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  status = apply_action(control, action, registers);
  if (status == NPU_RUNTIME_SUCCESS)
    ++control->applied_action_count;
  else
    ++control->rejected_action_count;
  return status;
}

enum npu_runtime_result npu_wifi_rro_control_apply_station_bitmap(
    struct npu_wifi_rro_control *control, uint32_t action, uint32_t value,
    struct npu_wifi_rro_station_bitmap_result *result) {
  uint32_t wcid = value & UINT32_C(0x7ff);
  uint32_t tid = (value >> 11U) & UINT32_C(0xf);
  volatile uint32_t *word;
  uint32_t bit_mask;
  uint32_t word_before;
  uint32_t word_after;

  if (result != NULL)
    *result = (struct npu_wifi_rro_station_bitmap_result){
        .wcid = wcid,
        .tid = tid,
    };
  if (control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (control->icv_error_table == NULL ||
      control->icv_error_word_count < NPU_WIFI_RRO_ICV_ERROR_STORAGE_WORD_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (wcid > NPU_WIFI_RRO_WCID_MAXIMUM || tid > NPU_WIFI_RRO_TID_MAXIMUM ||
      action >= NPU_WIFI_RRO_STATION_BITMAP_ACTION_COUNT) {
    ++control->station_bitmap_rejected_count;
    return NPU_RUNTIME_OUT_OF_RANGE;
  }

  word = &control->icv_error_table[wcid];
  word_before = *word;
  word_after = word_before;
  bit_mask = UINT32_C(1) << tid;
  switch (action) {
  case NPU_WIFI_RRO_STATION_BITMAP_SET:
    word_after |= bit_mask;
    *word = word_after;
    break;
  case NPU_WIFI_RRO_STATION_BITMAP_CLEAR:
    word_after &= ~bit_mask;
    *word = word_after;
    break;
  case NPU_WIFI_RRO_STATION_BITMAP_QUERY:
    ++control->station_bitmap_query_count;
    break;
  case NPU_WIFI_RRO_STATION_BITMAP_CLEAR_ALL: {
    uint32_t index;

    for (index = 0U; index < NPU_WIFI_RRO_ICV_ERROR_WORD_COUNT; ++index)
      control->icv_error_table[index] = 0U;
    word_after = *word;
    ++control->station_bitmap_clear_generation;
    break;
  }
  default:
    return NPU_RUNTIME_OUT_OF_RANGE;
  }

  if (result != NULL) {
    result->word_before = word_before;
    result->word_after = word_after;
  }
  ++control->station_bitmap_operation_count;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
npu_wifi_rro_control_set_page_pool(struct npu_wifi_rro_control *control,
                                   uint32_t address) {
  enum npu_runtime_result status;

  if (control == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (control->set_page_pool_address == NULL) {
    ++control->page_pool_rejected_count;
    return NPU_RUNTIME_OUT_OF_RANGE;
  }
  if (control->page_pool_address_valid) {
    if (control->page_pool_address == address)
      return NPU_RUNTIME_SUCCESS;
    ++control->page_pool_rejected_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }
  if (control->ring_enabled != 0U || control->configuration_ready != 0U) {
    ++control->page_pool_rejected_count;
    return NPU_RUNTIME_OWNERSHIP_ERROR;
  }

  status = control->set_page_pool_address(control->page_pool_context, address);
  if (status != NPU_RUNTIME_SUCCESS) {
    ++control->page_pool_rejected_count;
    return status;
  }

  control->page_pool_address = address;
  control->page_pool_address_valid = true;
  ++control->page_pool_configuration_count;
  return NPU_RUNTIME_SUCCESS;
}

static bool set_delete_station(void *context, uint32_t action, uint32_t value) {
  return npu_wifi_rro_control_apply_station_bitmap(context, action, value,
                                                   NULL) == NPU_RUNTIME_SUCCESS;
}

static bool set_dram_ba_node_address(void *context, uint32_t address) {
  return npu_wifi_rro_control_set_page_pool(context, address) ==
         NPU_RUNTIME_SUCCESS;
}

static bool
set_inode_txrx_registers(void *context, uint32_t interface,
                         const struct npu_wifi_inode_registers *registers) {
  return npu_wifi_rro_control_apply(context, interface, registers) ==
         NPU_RUNTIME_SUCCESS;
}

const struct npu_wifi_backend_operations
    npu_wifi_rro_control_backend_operations = {
        .set_delete_station = set_delete_station,
        .set_dram_ba_node_address = set_dram_ba_node_address,
        .set_inode_txrx_registers = set_inode_txrx_registers,
};

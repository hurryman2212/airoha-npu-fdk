/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/wifi/rro_page_backend.h"

#include "an7581/runtime/memory.h"

static bool
mapping_configuration_is_valid(uint32_t page_pool_base, uint32_t page_count,
                               const volatile void *record_mapping,
                               const volatile void *trailer_mapping,
                               const volatile uint16_t *release_queue,
                               const volatile uint32_t *release_counter) {
  uint32_t pool_size;

  if (record_mapping == NULL || trailer_mapping == NULL ||
      release_queue == NULL || page_count == 0U ||
      page_count > UINT32_C(0x10000) ||
      (page_pool_base & (NPU_WIFI_RRO_METADATA_PAGE_SIZE - 1U)) != 0U ||
      ((uintptr_t)record_mapping & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)trailer_mapping & (sizeof(uint32_t) - 1U)) != 0U ||
      ((uintptr_t)release_queue & (sizeof(uint16_t) - 1U)) != 0U ||
      (release_counter != NULL &&
       ((uintptr_t)release_counter & (sizeof(uint32_t) - 1U)) != 0U))
    return false;
  pool_size = page_count * NPU_WIFI_RRO_METADATA_PAGE_SIZE;
  return page_pool_base <=
         NPU_WIFI_RRO_PAGE_POOL_ADDRESS_LIMIT - pool_size + 1U;
}

enum npu_runtime_result npu_wifi_rro_page_backend_initialize(
    struct npu_wifi_rro_page_backend *backend, uint32_t page_pool_base,
    uint32_t page_count, volatile void *record_mapping,
    volatile void *trailer_mapping, volatile uint16_t *release_queue,
    volatile uint32_t *release_counter, npu_wifi_rro_cache_discard discard,
    void *discard_context, npu_wifi_rro_record_consume consume,
    void *consume_context) {
  if (backend == NULL || discard == NULL || consume == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!mapping_configuration_is_valid(page_pool_base, page_count,
                                      record_mapping, trailer_mapping,
                                      release_queue, release_counter))
    return NPU_RUNTIME_OUT_OF_RANGE;

  (void)npu_memset(backend, 0U, sizeof(*backend));
  backend->record_mapping = record_mapping;
  backend->trailer_mapping = trailer_mapping;
  backend->release_queue = release_queue;
  backend->release_counter = release_counter;
  backend->discard = discard;
  backend->consume = consume;
  backend->discard_context = discard_context;
  backend->consume_context = consume_context;
  backend->page_pool_base = page_pool_base;
  backend->page_count = page_count;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
page_offset(const struct npu_wifi_rro_page_backend *backend,
            uint32_t page_address, uint32_t *offset) {
  uint32_t page_id;

  if (page_address < backend->page_pool_base)
    return NPU_RUNTIME_OUT_OF_RANGE;
  *offset = page_address - backend->page_pool_base;
  if ((*offset & (NPU_WIFI_RRO_METADATA_PAGE_SIZE - 1U)) != 0U)
    return NPU_RUNTIME_OUT_OF_RANGE;
  page_id = *offset / NPU_WIFI_RRO_METADATA_PAGE_SIZE;
  if (page_id >= backend->page_count)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
map_page(void *context, uint32_t page_address,
         struct npu_wifi_rro_metadata_page_view *view) {
  struct npu_wifi_rro_page_backend *backend = context;
  volatile uint8_t *record_page;
  volatile uint8_t *trailer_page;
  uint32_t offset;
  enum npu_runtime_result status;

  if (backend == NULL || view == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  status = page_offset(backend, page_address, &offset);
  if (status != NPU_RUNTIME_SUCCESS)
    return status;

  record_page = backend->record_mapping + offset;
  trailer_page = backend->trailer_mapping + offset;
  view->records = __builtin_assume_aligned(record_page, sizeof(uint32_t));
  view->next_page_address =
      __builtin_assume_aligned(trailer_page + UINT32_C(0x78), sizeof(uint32_t));
  view->readiness =
      __builtin_assume_aligned(trailer_page + UINT32_C(0x7c), sizeof(uint32_t));
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result discard_line(void *context,
                                            uint32_t line_address) {
  struct npu_wifi_rro_page_backend *backend = context;
  uint32_t pool_size;
  uint32_t aliased_address;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  pool_size = backend->page_count * NPU_WIFI_RRO_METADATA_PAGE_SIZE;
  if (line_address < backend->page_pool_base ||
      line_address >= backend->page_pool_base + pool_size)
    return NPU_RUNTIME_OUT_OF_RANGE;
  aliased_address = (line_address & NPU_WIFI_RRO_PHYSICAL_ADDRESS_MASK) |
                    NPU_WIFI_RRO_RECORD_ALIAS_BIT;
  return backend->discard(backend->discard_context, aliased_address);
}

static enum npu_runtime_result
consume_record(void *context, const struct npu_wifi_rro_metadata_record *record,
               uint32_t record_index, uint32_t page_address,
               uint16_t page_slot) {
  struct npu_wifi_rro_page_backend *backend = context;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return backend->consume(backend->consume_context, record, record_index,
                          page_address, page_slot);
}

static enum npu_runtime_result release_page(void *context, uint16_t page_id) {
  struct npu_wifi_rro_page_backend *backend = context;
  uint32_t next_producer;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if ((uint32_t)page_id >= backend->page_count ||
      (uint32_t)backend->release_producer >=
          NPU_WIFI_RRO_PAGE_RELEASE_QUEUE_SIZE)
    return NPU_RUNTIME_OUT_OF_RANGE;

  backend->release_queue[backend->release_producer] = page_id;
  next_producer = (uint32_t)backend->release_producer + 1U;
  backend->release_producer =
      (uint16_t)(next_producer & (NPU_WIFI_RRO_PAGE_RELEASE_QUEUE_SIZE - 1U));
  if (backend->release_counter != NULL)
    ++*backend->release_counter;
  return NPU_RUNTIME_SUCCESS;
}

const struct npu_wifi_rro_page_operations npu_wifi_rro_page_backend_operations =
    {
        .map = map_page,
        .discard = discard_line,
        .consume = consume_record,
        .release = release_page,
};

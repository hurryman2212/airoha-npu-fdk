/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/ppe/mailbox.h"

#include "an7581/runtime/endian.h"
#include "an7581/runtime/memory.h"

static bool ppe_function_is_valid(uint32_t function) {
  return function >= NPU_PPE_FUNCTION_HWNAT_INITIALIZE &&
         function <= NPU_PPE_FUNCTION_L4S_UNSUPPORTED;
}

bool npu_ppe_mailbox_decode(const void *buffer, size_t length,
                            struct npu_ppe_request *request) {
  const uint8_t *bytes = buffer;

  if (buffer == NULL || request == NULL || length != NPU_PPE_MAILBOX_WIRE_SIZE)
    return false;

  request->operation = npu_load_little_endian_u32(bytes);
  request->function = npu_load_little_endian_u32(bytes + sizeof(uint32_t));
  if (request->operation != NPU_PPE_OPERATION_SET ||
      !ppe_function_is_valid(request->function))
    return false;

  switch (request->function) {
  case NPU_PPE_FUNCTION_HWNAT_DEINITIALIZE:
  case NPU_PPE_FUNCTION_FLOW_STATISTICS_UNSUPPORTED:
  case NPU_PPE_FUNCTION_L4S_UNSUPPORTED:
    return true;
  case NPU_PPE_FUNCTION_HWNAT_INITIALIZE:
    request->data.initialize.cds = bytes[8];
    request->data.initialize.xpon_hal_api = bytes[9];
    request->data.initialize.wan_xsi = bytes[10];
    request->data.initialize.ct_joyme4 = bytes[11];
    request->data.initialize.max_packet = bytes[12];
    request->data.initialize.ppe_type = npu_load_little_endian_u32(bytes + 16U);
    request->data.initialize.wan_mode = npu_load_little_endian_u32(bytes + 20U);
    request->data.initialize.wan_selection =
        npu_load_little_endian_u32(bytes + 24U);
    return true;
  case NPU_PPE_FUNCTION_API:
    request->data.api.function = npu_load_little_endian_u32(bytes + 8U);
    request->data.api.size = npu_load_little_endian_u32(bytes + 12U);
    request->data.api.data = npu_load_little_endian_u32(bytes + 16U);
    return request->data.api.function <= NPU_PPE_API_SRAM_RESET_VALUE;
  default:
    return false;
  }
}

void npu_ppe_state_set_backend(struct npu_ppe_state *state,
                               const struct npu_ppe_backend_operations *backend,
                               void *backend_context) {
  if (state == NULL)
    return;

  state->backend = backend;
  state->backend_context = backend_context;
}

static bool ppe_handle_initialize(struct npu_ppe_state *state,
                                  const struct npu_ppe_request *request) {
  if (state->backend->initialize == NULL ||
      !state->backend->initialize(state->backend_context,
                                  &request->data.initialize))
    return false;

  state->initialized = true;
  return true;
}

static bool ppe_handle_deinitialize(struct npu_ppe_state *state) {
  if (state->backend->deinitialize == NULL ||
      !state->backend->deinitialize(state->backend_context))
    return false;

  state->initialized = false;
  return true;
}

static bool ppe_handle_api(struct npu_ppe_state *state,
                           const struct npu_ppe_api_request *request) {
  switch (request->function) {
  case NPU_PPE_API_PPE2_SRAM_SET_ENTRY:
  case NPU_PPE_API_PPE_SRAM_SET_ENTRY:
    return state->backend->set_sram_entry != NULL &&
           state->backend->set_sram_entry(state->backend_context,
                                          request->function ==
                                              NPU_PPE_API_PPE2_SRAM_SET_ENTRY,
                                          request->data, request->size);
  case NPU_PPE_API_SRAM_SET_VALUE:
    return state->backend->commit_sram_entry != NULL &&
           state->backend->commit_sram_entry(state->backend_context,
                                             request->data, request->size);
  case NPU_PPE_API_SRAM_RESET_VALUE:
    return state->backend->reset_sram_entries != NULL &&
           state->backend->reset_sram_entries(state->backend_context,
                                              request->size);
  default:
    return false;
  }
}

static bool ppe_execute_request(struct npu_ppe_state *state,
                                const struct npu_ppe_request *request) {
  switch (request->function) {
  case NPU_PPE_FUNCTION_HWNAT_INITIALIZE:
    return ppe_handle_initialize(state, request);
  case NPU_PPE_FUNCTION_HWNAT_DEINITIALIZE:
    return ppe_handle_deinitialize(state);
  case NPU_PPE_FUNCTION_API:
    return ppe_handle_api(state, &request->data.api);
  case NPU_PPE_FUNCTION_L4S_UNSUPPORTED:
    return true;
  case NPU_PPE_FUNCTION_FLOW_STATISTICS_UNSUPPORTED:
  default:
    return false;
  }
}

bool npu_ppe_mailbox_handle(struct npu_ppe_state *state, void *buffer,
                            size_t length) {
  struct npu_ppe_request request;
  bool success;

  if (state == NULL)
    return false;

  if (!npu_ppe_mailbox_decode(buffer, length, &request)) {
    ++state->invalid_requests;
    return false;
  }

  (void)npu_memcpy(&state->last_request, &request, sizeof(request));
  state->last_request_valid = true;
  ++state->decoded_requests;

  if (state->backend == NULL) {
    ++state->rejected_requests;
    return false;
  }

  success = ppe_execute_request(state, &request);
  if (success)
    ++state->successful_requests;
  else
    ++state->rejected_requests;
  return success;
}

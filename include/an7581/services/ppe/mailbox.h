/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_PPE_MAILBOX_H
#define NPU_PPE_MAILBOX_H

#include "an7581/platform/types.h"

#define NPU_PPE_MAILBOX_WIRE_SIZE 28U
#define NPU_PPE_SRAM_ENTRY_SIZE 80U
#define NPU_PPE_SRAM_ENTRY_COUNT UINT32_C(0x00004000)

enum npu_ppe_operation {
  NPU_PPE_OPERATION_SET = 1,
  NPU_PPE_OPERATION_SET_NO_WAIT,
  NPU_PPE_OPERATION_GET,
  NPU_PPE_OPERATION_GET_NO_WAIT,
};

enum npu_ppe_function {
  NPU_PPE_FUNCTION_SET_WAIT = 0,
  NPU_PPE_FUNCTION_HWNAT_INITIALIZE,
  NPU_PPE_FUNCTION_HWNAT_DEINITIALIZE,
  NPU_PPE_FUNCTION_API,
  NPU_PPE_FUNCTION_FLOW_STATISTICS_UNSUPPORTED,
  NPU_PPE_FUNCTION_L4S_UNSUPPORTED,
};

enum npu_ppe_api_function {
  NPU_PPE_API_PPE2_SRAM_SET_ENTRY = 0,
  NPU_PPE_API_PPE_SRAM_SET_ENTRY,
  NPU_PPE_API_SRAM_SET_VALUE,
  NPU_PPE_API_SRAM_RESET_VALUE,
};

struct npu_ppe_initialize_request {
  uint8_t cds;
  uint8_t xpon_hal_api;
  uint8_t wan_xsi;
  uint8_t ct_joyme4;
  uint8_t max_packet;
  uint32_t ppe_type;
  uint32_t wan_mode;
  uint32_t wan_selection;
};

struct npu_ppe_api_request {
  uint32_t function;
  uint32_t size;
  uint32_t data;
};

struct npu_ppe_request {
  uint32_t operation;
  uint32_t function;
  union {
    struct npu_ppe_initialize_request initialize;
    struct npu_ppe_api_request api;
  } data;
};

struct npu_ppe_backend_operations {
  bool (*initialize)(void *context,
                     const struct npu_ppe_initialize_request *request);
  bool (*deinitialize)(void *context);
  bool (*set_sram_entry)(void *context, bool ppe2, uint32_t dma_address,
                         uint32_t size);
  bool (*commit_sram_entry)(void *context, uint32_t entry, uint32_t size);
  bool (*reset_sram_entries)(void *context, uint32_t entry_count);
};

struct npu_ppe_state {
  struct npu_ppe_request last_request;
  const struct npu_ppe_backend_operations *backend;
  void *backend_context;
  uint32_t decoded_requests;
  uint32_t invalid_requests;
  uint32_t successful_requests;
  uint32_t rejected_requests;
  bool last_request_valid;
  bool initialized;
};

bool npu_ppe_mailbox_decode(const void *buffer, size_t length,
                            struct npu_ppe_request *request);
void npu_ppe_state_set_backend(struct npu_ppe_state *state,
                               const struct npu_ppe_backend_operations *backend,
                               void *backend_context);
bool npu_ppe_mailbox_handle(struct npu_ppe_state *state, void *buffer,
                            size_t length);

#endif

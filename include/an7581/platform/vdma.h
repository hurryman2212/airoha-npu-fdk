/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_VDMA_H
#define AN7581_VDMA_H

#include "an7581/platform/types.h"
#include "an7581/runtime/status.h"

#define AN7581_VDMA_CHANNEL_BASE UINT32_C(0x1fb30000)
#define AN7581_VDMA_CHANNEL_STRIDE UINT32_C(0x10)
#define AN7581_VDMA_COMPLETION_STATUS UINT32_C(0x1fb30204)
#define AN7581_VDMA_CHANNEL_COUNT UINT32_C(32)
#define AN7581_VDMA_TRANSFER_CONTROL UINT32_C(0x23)
#define AN7581_VDMA_TRANSFER_LENGTH_LIMIT UINT32_C(0xffff)

enum npu_runtime_result an7581_vdma_copy(uint32_t channel,
                                         uint32_t source_address,
                                         uint32_t destination_address,
                                         uint32_t length, uint32_t poll_limit);

#endif

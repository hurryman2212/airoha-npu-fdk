/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_PPE_H
#define AN7581_PPE_H

#include "an7581/services/ppe/mailbox.h"

#define AN7581_PPE_INSTANCE_COUNT 2U
#define AN7581_PPE_ACK_POLL_ATTEMPTS 10U

struct an7581_ppe_runtime {
  struct npu_ppe_initialize_request configuration;
  bool initialized;
};

bool an7581_ppe_runtime_reset(struct an7581_ppe_runtime *runtime);
const struct npu_ppe_backend_operations *an7581_ppe_backend_operations(void);

#endif

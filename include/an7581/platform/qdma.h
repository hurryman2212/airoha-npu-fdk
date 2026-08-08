/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_QDMA_H
#define AN7581_QDMA_H

#include "an7581/platform/types.h"

#define AN7581_QDMA_DESCRIPTOR_SIZE UINT32_C(0x20)

/* EN7581 QDMA descriptor, in hardware word order. */
struct an7581_qdma_descriptor {
  uint32_t tcp_timestamp_reply;
  uint32_t control;
  uint32_t buffer_address;
  uint32_t next_descriptor;
  uint32_t message[4];
};

#endif

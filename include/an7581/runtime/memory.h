/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_RUNTIME_MEMORY_H
#define NPU_RUNTIME_MEMORY_H

#include "an7581/platform/types.h"

void *npu_memset(void *destination, uint8_t value, size_t length);
void *npu_memcpy(void *destination, const void *source, size_t length);
void *npu_memmove(void *destination, const void *source, size_t length);

#ifndef AN7581_MMIO_TEST
void *memset(void *destination, int value, size_t length);
void *memcpy(void *destination, const void *source, size_t length);
#endif

#endif

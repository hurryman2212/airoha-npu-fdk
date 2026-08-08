/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/runtime/memory.h"

void *npu_memset(void *destination, uint8_t value, size_t length) {
  uint8_t *bytes = destination;
  size_t index;

  for (index = 0U; index < length; ++index)
    bytes[index] = value;

  return destination;
}

void *npu_memcpy(void *destination, const void *source, size_t length) {
  uint8_t *destination_bytes = destination;
  const uint8_t *source_bytes = source;
  size_t index;

  for (index = 0U; index < length; ++index)
    destination_bytes[index] = source_bytes[index];

  return destination;
}

void *npu_memmove(void *destination, const void *source, size_t length) {
  uint8_t *destination_bytes = destination;
  const uint8_t *source_bytes = source;
  size_t index;

  if ((uintptr_t)destination_bytes < (uintptr_t)source_bytes) {
    for (index = 0U; index < length; ++index)
      destination_bytes[index] = source_bytes[index];
  } else if ((uintptr_t)destination_bytes > (uintptr_t)source_bytes) {
    for (index = length; index > 0U; --index)
      destination_bytes[index - 1U] = source_bytes[index - 1U];
  }

  return destination;
}

#ifndef AN7581_MMIO_TEST
void *memset(void *destination, int value, size_t length) {
  return npu_memset(destination, (uint8_t)value, length);
}

void *memcpy(void *destination, const void *source, size_t length) {
  return npu_memcpy(destination, source, length);
}
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NPU_RUNTIME_ENDIAN_H
#define NPU_RUNTIME_ENDIAN_H

#include "an7581/platform/types.h"

static inline uint16_t npu_load_little_endian_u16(const uint8_t *data) {
  return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8));
}

static inline uint32_t npu_load_little_endian_u32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static inline uint16_t npu_load_big_endian_u16(const uint8_t *data) {
  return (uint16_t)((uint16_t)((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static inline void npu_store_little_endian_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

static inline void npu_store_big_endian_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
}

#endif

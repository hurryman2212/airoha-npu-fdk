/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/data_image.h"

#include "an7581/runtime/endian.h"

#define AN7581_DATA_IMAGE_MAGIC_OFFSET 0U
#define AN7581_DATA_IMAGE_FORMAT_OFFSET 4U
#define AN7581_DATA_IMAGE_HEADER_SIZE_OFFSET 6U
#define AN7581_DATA_IMAGE_COMPATIBILITY_MAJOR_OFFSET 8U
#define AN7581_DATA_IMAGE_COMPATIBILITY_MINOR_OFFSET 10U
#define AN7581_DATA_IMAGE_FLAGS_OFFSET 12U
#define AN7581_DATA_IMAGE_PAYLOAD_SIZE_OFFSET 16U
#define AN7581_DATA_IMAGE_HEADER_RESERVED_OFFSET 20U
#define AN7581_DATA_IMAGE_HEADER_RESERVED_SIZE 12U

#define AN7581_DATA_IMAGE_PAYLOAD_VERSION_OFFSET 0U
#define AN7581_DATA_IMAGE_TUNNEL_PRESENT_OFFSET 4U
#define AN7581_DATA_IMAGE_VXLAN_MTU_OFFSET 8U
#define AN7581_DATA_IMAGE_FRAGMENT_MTU_OFFSET 12U

#define AN7581_DATA_IMAGE_TUNNEL_VXLAN_PRESENT UINT32_C(1)
#define AN7581_DATA_IMAGE_TUNNEL_FRAGMENT_PRESENT_BASE UINT32_C(2)
#define AN7581_DATA_IMAGE_TUNNEL_PRESENT_MASK UINT32_C(0x1f)
#define AN7581_DATA_IMAGE_MAX_MTU UINT32_C(0xffff)

const char an7581_firmware_version_string[]
    __attribute__((section(".rodata.firmware_version"), used)) =
        AN7581_FIRMWARE_VERSION_STRING;

static bool data_image_magic_is_valid(const uint8_t *image) {
  return image[AN7581_DATA_IMAGE_MAGIC_OFFSET] == (uint8_t)'A' &&
         image[AN7581_DATA_IMAGE_MAGIC_OFFSET + 1U] == (uint8_t)'N' &&
         image[AN7581_DATA_IMAGE_MAGIC_OFFSET + 2U] == (uint8_t)'P' &&
         image[AN7581_DATA_IMAGE_MAGIC_OFFSET + 3U] == (uint8_t)'U';
}

static bool bytes_are_zero(const uint8_t *data, size_t length) {
  size_t index;

  for (index = 0U; index < length; ++index) {
    if (data[index] != 0U)
      return false;
  }
  return true;
}

static enum npu_runtime_result
decode_tunnel_defaults(const uint8_t *payload,
                       struct an7581_data_configuration *configuration) {
  uint32_t present_fields;
  uint32_t value;
  size_t index;

  /* The MT7996-only tunnel payload schema is version 1. */
  if (npu_load_little_endian_u32(
          payload + AN7581_DATA_IMAGE_PAYLOAD_VERSION_OFFSET) != 1U)
    return NPU_RUNTIME_REJECTED;

  present_fields = npu_load_little_endian_u32(
      payload + AN7581_DATA_IMAGE_TUNNEL_PRESENT_OFFSET);
  if ((present_fields & ~AN7581_DATA_IMAGE_TUNNEL_PRESENT_MASK) != 0U)
    return NPU_RUNTIME_REJECTED;

  value =
      npu_load_little_endian_u32(payload + AN7581_DATA_IMAGE_VXLAN_MTU_OFFSET);
  configuration->vxlan_mtu_valid =
      (present_fields & AN7581_DATA_IMAGE_TUNNEL_VXLAN_PRESENT) != 0U;
  if ((configuration->vxlan_mtu_valid && value > AN7581_DATA_IMAGE_MAX_MTU) ||
      (!configuration->vxlan_mtu_valid && value != 0U))
    return NPU_RUNTIME_OUT_OF_RANGE;
  configuration->vxlan_mtu = value;

  for (index = 0U; index < NPU_TUNNEL_FRAGMENT_MTU_COUNT; ++index) {
    uint32_t present = AN7581_DATA_IMAGE_TUNNEL_FRAGMENT_PRESENT_BASE << index;

    value = npu_load_little_endian_u32(payload +
                                       AN7581_DATA_IMAGE_FRAGMENT_MTU_OFFSET +
                                       index * sizeof(uint32_t));
    configuration->fragment_mtu_valid[index] = (present_fields & present) != 0U;
    if ((configuration->fragment_mtu_valid[index] &&
         value > AN7581_DATA_IMAGE_MAX_MTU) ||
        (!configuration->fragment_mtu_valid[index] && value != 0U))
      return NPU_RUNTIME_OUT_OF_RANGE;
    configuration->fragment_mtu[index] = value;
  }

  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result
an7581_data_image_decode(const uint8_t *image, size_t extent,
                         struct an7581_data_configuration *configuration) {
  struct an7581_data_configuration candidate = {0};
  enum npu_runtime_result result;
  uint32_t payload_size;
  uint16_t format_version;
  uint16_t header_size;

  if (image == NULL || configuration == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (extent < AN7581_DATA_IMAGE_HEADER_SIZE)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!data_image_magic_is_valid(image))
    return NPU_RUNTIME_REJECTED;

  format_version =
      npu_load_little_endian_u16(image + AN7581_DATA_IMAGE_FORMAT_OFFSET);
  header_size =
      npu_load_little_endian_u16(image + AN7581_DATA_IMAGE_HEADER_SIZE_OFFSET);
  if (npu_load_little_endian_u16(
          image + AN7581_DATA_IMAGE_COMPATIBILITY_MAJOR_OFFSET) !=
          NPU_FIRMWARE_VERSION_MAJOR ||
      npu_load_little_endian_u16(
          image + AN7581_DATA_IMAGE_COMPATIBILITY_MINOR_OFFSET) !=
          NPU_FIRMWARE_VERSION_MINOR)
    return NPU_RUNTIME_REJECTED;
  candidate.flags =
      npu_load_little_endian_u32(image + AN7581_DATA_IMAGE_FLAGS_OFFSET);
  payload_size =
      npu_load_little_endian_u32(image + AN7581_DATA_IMAGE_PAYLOAD_SIZE_OFFSET);

  if (header_size != AN7581_DATA_IMAGE_HEADER_SIZE ||
      !bytes_are_zero(image + AN7581_DATA_IMAGE_HEADER_RESERVED_OFFSET,
                      AN7581_DATA_IMAGE_HEADER_RESERVED_SIZE))
    return NPU_RUNTIME_REJECTED;
  if (payload_size > extent - AN7581_DATA_IMAGE_HEADER_SIZE)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (extent != AN7581_DATA_IMAGE_HEADER_SIZE + payload_size)
    return NPU_RUNTIME_REJECTED;

  /* The MT7996-only AN7581 data-image container uses format version 2. */
  if (format_version != 2U || candidate.flags != 0U ||
      payload_size != AN7581_DATA_IMAGE_TUNNEL_PAYLOAD_SIZE)
    return NPU_RUNTIME_REJECTED;
  result =
      decode_tunnel_defaults(image + AN7581_DATA_IMAGE_HEADER_SIZE, &candidate);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;

  candidate.valid = true;
  *configuration = candidate;
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result an7581_data_configuration_apply_tunnel(
    const struct an7581_data_configuration *configuration,
    struct npu_tunnel_state *state) {
  size_t index;

  if (configuration == NULL || state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!configuration->valid)
    return NPU_RUNTIME_REJECTED;

  if (configuration->vxlan_mtu_valid) {
    state->vxlan_mtu = configuration->vxlan_mtu;
    state->vxlan_mtu_valid = true;
  }
  for (index = 0U; index < NPU_TUNNEL_FRAGMENT_MTU_COUNT; ++index) {
    if (!configuration->fragment_mtu_valid[index])
      continue;
    state->fragment_mtu[index].value = configuration->fragment_mtu[index];
    state->fragment_mtu[index].valid = true;
  }
  return NPU_RUNTIME_SUCCESS;
}

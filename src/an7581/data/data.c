/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/data_image.h"

struct an7581_data_image_compatibility {
  uint16_t major;
  uint16_t minor;
};

struct an7581_data_image_header {
  uint8_t magic[4];
  uint16_t format_version;
  uint16_t header_size;
  struct an7581_data_image_compatibility compatibility;
  uint32_t flags;
  uint32_t payload_size;
  uint32_t reserved[3];
};

struct an7581_tunnel_data_image {
  uint32_t schema_version;
  uint32_t present_fields;
  uint32_t vxlan_mtu;
  uint32_t fragment_mtu[NPU_TUNNEL_FRAGMENT_MTU_COUNT];
};

struct an7581_data_image_layout {
  struct an7581_data_image_header header;
  struct an7581_tunnel_data_image tunnel;
};

enum an7581_tunnel_data_present_field {
  AN7581_TUNNEL_DATA_VXLAN_MTU_PRESENT = UINT32_C(1) << 0,
  AN7581_TUNNEL_DATA_FRAGMENT_MTU_0_PRESENT = UINT32_C(1) << 1,
};

_Static_assert(offsetof(struct an7581_data_image_header, format_version) == 4U,
               "data-image format-version offset changed");
_Static_assert(offsetof(struct an7581_data_image_header, compatibility) == 8U,
               "data-image compatibility offset changed");
_Static_assert(offsetof(struct an7581_data_image_header, flags) == 12U,
               "data-image flags offset changed");
_Static_assert(offsetof(struct an7581_data_image_header, payload_size) == 16U,
               "data-image payload-size offset changed");
_Static_assert(offsetof(struct an7581_data_image_header, reserved) == 20U,
               "data-image reserved offset changed");
_Static_assert(sizeof(struct an7581_data_image_header) ==
                   AN7581_DATA_IMAGE_HEADER_SIZE,
               "data-image header size changed");
_Static_assert(sizeof(struct an7581_tunnel_data_image) ==
                   AN7581_DATA_IMAGE_TUNNEL_PAYLOAD_SIZE,
               "tunnel data-image size changed");
_Static_assert(offsetof(struct an7581_data_image_layout, tunnel) ==
                   AN7581_DATA_IMAGE_HEADER_SIZE,
               "tunnel data-image offset changed");
_Static_assert(sizeof(struct an7581_data_image_layout) ==
                   AN7581_DATA_IMAGE_CURRENT_SIZE,
               "AN7581 data-image size changed");

/* The builder serializes this little-endian RV32 object without translation. */
struct an7581_data_image_layout an7581_platform_data_image __attribute__((
    section(".data.platform"), used, aligned(sizeof(uint32_t)))) = {
    .header =
        {
            .magic = {'A', 'N', 'P', 'U'},
            .format_version = 2U,
            .header_size = AN7581_DATA_IMAGE_HEADER_SIZE,
            .compatibility = {.major = NPU_FIRMWARE_VERSION_MAJOR,
                              .minor = NPU_FIRMWARE_VERSION_MINOR},
            .flags = 0U,
            .payload_size = AN7581_DATA_IMAGE_TUNNEL_PAYLOAD_SIZE,
            .reserved = {0U, 0U, 0U},
        },
    .tunnel =
        {
            .schema_version = 1U,
            .present_fields = AN7581_TUNNEL_DATA_VXLAN_MTU_PRESENT |
                              AN7581_TUNNEL_DATA_FRAGMENT_MTU_0_PRESENT,
            .vxlan_mtu = 1500U,
            .fragment_mtu = {1500U, 0U, 0U, 0U},
        },
};

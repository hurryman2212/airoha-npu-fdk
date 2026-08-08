/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_DATA_IMAGE_H
#define AN7581_DATA_IMAGE_H

#include "an7581/runtime/status.h"
#include "an7581/services/tunnel/mailbox.h"
#include "an7581/services/wifi/mailbox.h"

#define AN7581_DATA_IMAGE_HEADER_SIZE 32U
#define AN7581_DATA_IMAGE_TUNNEL_PAYLOAD_SIZE 28U
#define AN7581_DATA_IMAGE_CURRENT_SIZE                                         \
  (AN7581_DATA_IMAGE_HEADER_SIZE + AN7581_DATA_IMAGE_TUNNEL_PAYLOAD_SIZE)
#define AN7581_FIRMWARE_VERSION_STRING "TLB7.7.0.0_v03"

extern const char an7581_firmware_version_string[];

struct an7581_data_configuration {
  uint32_t flags;
  uint32_t vxlan_mtu;
  uint32_t fragment_mtu[NPU_TUNNEL_FRAGMENT_MTU_COUNT];
  bool fragment_mtu_valid[NPU_TUNNEL_FRAGMENT_MTU_COUNT];
  bool vxlan_mtu_valid;
  bool valid;
};

enum npu_runtime_result
an7581_data_image_decode(const uint8_t *image, size_t extent,
                         struct an7581_data_configuration *configuration);
enum npu_runtime_result an7581_data_configuration_apply_tunnel(
    const struct an7581_data_configuration *configuration,
    struct npu_tunnel_state *state);

#endif

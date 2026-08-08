/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/runtime.h"

#include "an7581/runtime/endian.h"

#define NPU_TUNNEL_ETHERTYPE_OFFSET (NPU_TUNNEL_PACKET_CONTROL_SIZE + 12U)
#define NPU_TUNNEL_VLAN_INNER_ETHERTYPE_OFFSET                                 \
  (NPU_TUNNEL_PACKET_CONTROL_SIZE + 16U)
#define NPU_TUNNEL_ETHERTYPE_PPPOE UINT16_C(0x8864)
#define NPU_TUNNEL_ETHERTYPE_VLAN UINT16_C(0x8100)

static bool
packet_request_valid(const struct npu_tunnel_packet_request *request) {
  uint32_t wire_length;

  if (request == NULL || request->packet == NULL ||
      request->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      request->packet_extent < NPU_TUNNEL_PACKET_CONTROL_SIZE)
    return false;

  wire_length = npu_load_little_endian_u16(request->packet);
  return wire_length <= UINT16_MAX - NPU_TUNNEL_PACKET_CONTROL_SIZE &&
         (size_t)NPU_TUNNEL_PACKET_CONTROL_SIZE + wire_length <=
             request->packet_extent;
}

static uint8_t
fragment_pppoe_length_offset(const struct npu_tunnel_packet_request *request) {
  uint32_t wire_length = npu_load_little_endian_u16(request->packet);
  size_t received_length = NPU_TUNNEL_PACKET_CONTROL_SIZE + wire_length;

  if (NPU_TUNNEL_ETHERTYPE_OFFSET + sizeof(uint16_t) <= received_length &&
      npu_load_big_endian_u16(request->packet + NPU_TUNNEL_ETHERTYPE_OFFSET) ==
          NPU_TUNNEL_ETHERTYPE_PPPOE)
    return NPU_TUNNEL_PPPOE_LENGTH_OFFSET_DIRECT;
  if (NPU_TUNNEL_VLAN_INNER_ETHERTYPE_OFFSET + sizeof(uint16_t) <=
          received_length &&
      npu_load_big_endian_u16(request->packet + NPU_TUNNEL_ETHERTYPE_OFFSET) ==
          NPU_TUNNEL_ETHERTYPE_VLAN &&
      npu_load_big_endian_u16(request->packet +
                              NPU_TUNNEL_VLAN_INNER_ETHERTYPE_OFFSET) ==
          NPU_TUNNEL_ETHERTYPE_PPPOE)
    return NPU_TUNNEL_PPPOE_LENGTH_OFFSET_VLAN;
  return 0U;
}

static enum npu_runtime_result fragment_execute(
    struct npu_tunnel_packet_runtime *runtime,
    const struct npu_tunnel_packet_request *request,
    const struct npu_tunnel_packet_classification *classification) {
  struct npu_tunnel_fragment_execution execution = {
      .packet = request->packet,
      .packet_extent = request->packet_extent,
      .packet_address = request->packet_address,
      .ipv6_fragment_header_address =
          runtime->config.ipv6_fragment_header_address,
      .channel = request->channel,
      .pppoe_length_offset = fragment_pppoe_length_offset(request),
  };
  struct npu_tunnel_fragment_plan plan;

  if (!npu_tunnel_fragment_plan(classification, &plan))
    return NPU_RUNTIME_REJECTED;
  return npu_tunnel_fragment_execute(&runtime->fragment, runtime->config.bridge,
                                     &plan, &execution);
}

static enum npu_runtime_result
reassembly_execute(struct npu_tunnel_packet_runtime *runtime,
                   const struct npu_tunnel_packet_request *request,
                   enum npu_tunnel_packet_disposition *disposition) {
  struct npu_tunnel_reassembly_packet packet = {
      .packet = request->packet,
      .packet_extent = request->packet_extent,
      .packet_address = request->packet_address,
      .channel = request->channel,
  };
  struct npu_tunnel_reassembly_operation_sequence sequence;
  enum npu_runtime_result result;

  if (!npu_tunnel_reassembly_operations_plan(&runtime->reassembly, &packet,
                                             &sequence))
    return NPU_RUNTIME_REJECTED;
  result = npu_tunnel_reassembly_execute(&runtime->reassembly,
                                         runtime->config.bridge, &packet);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;

  if (sequence.action == NPU_TUNNEL_REASSEMBLY_STORE_IPV4 ||
      sequence.action == NPU_TUNNEL_REASSEMBLY_STORE_IPV6)
    *disposition = NPU_TUNNEL_PACKET_DISPOSITION_REASSEMBLY_PENDING;
  else
    *disposition = NPU_TUNNEL_PACKET_DISPOSITION_BRIDGE;
  return NPU_RUNTIME_SUCCESS;
}

static bool
udf_state_apply(const struct npu_tunnel_packet_runtime *runtime,
                const struct npu_tunnel_packet_classification *classification,
                struct npu_tunnel_udf_request *udf_request) {
  const struct npu_tunnel_state *state = runtime->config.mailbox_state;
  uint8_t header_index;
  size_t index;

  if (classification->udf <= 20U) {
    if (!state->vxlan_mtu_valid || state->vxlan_mtu > UINT16_MAX)
      return false;
    udf_request->vxlan_mtu = (uint16_t)state->vxlan_mtu;
  }

  if (classification->udf >= 41U && classification->udf <= 48U) {
    header_index = (uint8_t)(classification->udf - 41U);
    if (!state->srv6_header[header_index].valid)
      return false;
  }
  for (index = 0U; index < NPU_TUNNEL_UDF_SRV6_HEADER_COUNT; ++index) {
    if (state->srv6_header[index].valid)
      udf_request->srv6_header_lengths[index] =
          state->srv6_header[index].length;
  }

  if (classification->udf < 67U)
    return true;
  if (!state->map_info_base_valid || state->map_info_base == 0U ||
      runtime->config.map_info.template_scratch == NULL ||
      runtime->config.map_info.template_scratch_extent <
          NPU_TUNNEL_UDF_MAP_RECORD_SIZE ||
      runtime->config.map_info.template_scratch_address == 0U)
    return false;

  if (runtime->config.map_info.records != NULL) {
    if (state->map_info_base != runtime->config.map_info.physical_base)
      return false;
    udf_request->map_info_records = runtime->config.map_info.records;
    udf_request->map_info_extent = runtime->config.map_info.record_extent;
  } else {
    udf_request->map_info_records =
        (const uint8_t *)(uintptr_t)state->map_info_base;
    udf_request->map_info_extent =
        (size_t)(UINT32_MAX - state->map_info_base) + 1U;
  }
  udf_request->map_template_scratch = runtime->config.map_info.template_scratch;
  udf_request->map_template_scratch_extent =
      runtime->config.map_info.template_scratch_extent;
  udf_request->map_template_scratch_address =
      runtime->config.map_info.template_scratch_address;
  return true;
}

static enum npu_runtime_result
udf_execute(const struct npu_tunnel_packet_runtime *runtime,
            const struct npu_tunnel_packet_request *request,
            const struct npu_tunnel_packet_classification *classification) {
  struct npu_tunnel_udf_request udf_request = {
      .packet = request->packet,
      .packet_extent = request->packet_extent,
      .packet_address = request->packet_address,
      .template_base_address = runtime->config.udf_template_base_address,
      .channel = request->channel,
  };

  if (!udf_state_apply(runtime, classification, &udf_request))
    return NPU_RUNTIME_REJECTED;
  return npu_tunnel_udf_execute(&udf_request, runtime->config.bridge);
}

bool npu_tunnel_packet_runtime_initialize(
    struct npu_tunnel_packet_runtime *runtime,
    const struct npu_tunnel_packet_runtime_config *config) {
  struct npu_tunnel_packet_runtime initialized = {0};

  if (runtime == NULL || config == NULL || config->bridge == NULL ||
      config->mailbox_state == NULL || config->release == NULL ||
      !npu_tunnel_reassembly_runtime_initialize(
          &initialized.reassembly, config->release, config->release_context))
    return false;

  initialized.config = *config;
  *runtime = initialized;
  return true;
}

enum npu_runtime_result npu_tunnel_packet_runtime_synchronize(
    struct npu_tunnel_packet_runtime *runtime) {
  uint32_t requested;
  enum npu_runtime_result result;

  if (runtime == NULL || runtime->config.mailbox_state == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  requested = runtime->config.mailbox_state->debug.reassembly_flush_requests;
  if (requested < runtime->reassembly_flush_requests_consumed) {
    runtime->reassembly_flush_requests_consumed = requested;
    return NPU_RUNTIME_SUCCESS;
  }
  if (requested == runtime->reassembly_flush_requests_consumed)
    return NPU_RUNTIME_SUCCESS;

  result = npu_tunnel_reassembly_flush(&runtime->reassembly);
  if (result == NPU_RUNTIME_SUCCESS)
    runtime->reassembly_flush_requests_consumed = requested;
  return result;
}

enum npu_runtime_result npu_tunnel_packet_runtime_execute(
    struct npu_tunnel_packet_runtime *runtime,
    const struct npu_tunnel_packet_request *request,
    struct npu_tunnel_packet_dispatch *dispatch) {
  struct npu_tunnel_packet_dispatch decoded = {0};
  enum npu_runtime_result result;

  if (runtime == NULL || dispatch == NULL || request == NULL ||
      request->packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (request->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;
  if (!packet_request_valid(request))
    return NPU_RUNTIME_OUT_OF_RANGE;

  result = npu_tunnel_packet_runtime_synchronize(runtime);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;
  if (!npu_tunnel_packet_classify(request->packet, request->packet_extent,
                                  &decoded.classification) ||
      decoded.classification.ip_version_ambiguous)
    return NPU_RUNTIME_REJECTED;

  if (decoded.classification.route == NPU_TUNNEL_PACKET_ROUTE_FRAGMENT) {
    result = fragment_execute(runtime, request, &decoded.classification);
    decoded.disposition = NPU_TUNNEL_PACKET_DISPOSITION_BRIDGE;
  } else if (decoded.classification.route ==
             NPU_TUNNEL_PACKET_ROUTE_REASSEMBLE) {
    result = reassembly_execute(runtime, request, &decoded.disposition);
  } else {
    result = udf_execute(runtime, request, &decoded.classification);
    decoded.disposition = NPU_TUNNEL_PACKET_DISPOSITION_BRIDGE;
  }

  if (result == NPU_RUNTIME_SUCCESS)
    *dispatch = decoded;
  return result;
}

enum npu_runtime_result npu_tunnel_packet_runtime_ingress_step(
    struct npu_tunnel_packet_runtime *runtime,
    struct npu_tunnel_bridge_ingress *ingress, uint8_t channel,
    struct npu_tunnel_packet_ingress_result *result) {
  struct npu_tunnel_packet_ingress_result completed = {0};
  struct npu_tunnel_packet_request request;
  enum npu_runtime_result dequeue_result;

  if (runtime == NULL || ingress == NULL || result == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  dequeue_result =
      npu_tunnel_bridge_ingress_dequeue(ingress, channel, &completed.packet);
  if (dequeue_result != NPU_RUNTIME_SUCCESS)
    return dequeue_result;

  request = (struct npu_tunnel_packet_request){
      .packet = completed.packet.packet,
      .packet_extent = completed.packet.packet_extent,
      .packet_address = completed.packet.packet_address,
      .channel = completed.packet.channel,
  };
  completed.dispatch_result =
      npu_tunnel_packet_runtime_execute(runtime, &request, &completed.dispatch);
  completed.release_result = NPU_RUNTIME_SUCCESS;
  if (completed.dispatch_result != NPU_RUNTIME_SUCCESS) {
    if (completed.packet.received_length > UINT16_MAX)
      completed.release_result = NPU_RUNTIME_OUT_OF_RANGE;
    else
      completed.release_result = runtime->config.release(
          runtime->config.release_context, completed.packet.channel,
          completed.packet.packet_address,
          (uint16_t)completed.packet.received_length);
    if (completed.release_result == NPU_RUNTIME_SUCCESS)
      completed.dispatch.disposition = NPU_TUNNEL_PACKET_DISPOSITION_RELEASED;
    else
      completed.dispatch.disposition = NPU_TUNNEL_PACKET_DISPOSITION_CALLER;
  }

  *result = completed;
  return NPU_RUNTIME_SUCCESS;
}

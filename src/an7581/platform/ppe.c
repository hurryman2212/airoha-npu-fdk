/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/ppe.h"

#include "an7581/platform/dma.h"
#include "an7581/platform/mmio.h"
#include "an7581/runtime/memory.h"

#define AN7581_FE_BASE UINT32_C(0x1fb50000)
#define AN7581_PPE_INSTANCE_STRIDE UINT32_C(0x00001000)
#define AN7581_PPE1_OFFSET UINT32_C(0x00000c00)
#define AN7581_PPE2_OFFSET (AN7581_PPE1_OFFSET + AN7581_PPE_INSTANCE_STRIDE)
#define AN7581_PPE_GLOBAL_CONFIG_OFFSET UINT32_C(0x00000200)
#define AN7581_PPE_FLOW_CONFIG_OFFSET UINT32_C(0x00000204)
#define AN7581_PPE_IP_PROTOCOL_CHECK_OFFSET UINT32_C(0x00000208)
#define AN7581_PPE_IP_PROTOCOL_FILTER0_OFFSET UINT32_C(0x0000020c)
#define AN7581_PPE_IP_PROTOCOL_FILTER1_OFFSET UINT32_C(0x00000210)
#define AN7581_PPE_IP_PROTOCOL_FILTER2_OFFSET UINT32_C(0x00000214)
#define AN7581_PPE_IP_PROTOCOL_FILTER3_OFFSET UINT32_C(0x00000218)
#define AN7581_PPE_TABLE_CONFIG_OFFSET UINT32_C(0x0000021c)
#define AN7581_PPE_BIND_RATE_OFFSET UINT32_C(0x00000228)
#define AN7581_PPE_BIND_LIMIT0_OFFSET UINT32_C(0x0000022c)
#define AN7581_PPE_BIND_LIMIT1_OFFSET UINT32_C(0x00000230)
#define AN7581_PPE_KEEPALIVE_OFFSET UINT32_C(0x00000234)
#define AN7581_PPE_UNBIND_AGE_OFFSET UINT32_C(0x00000238)
#define AN7581_PPE_BIND_AGE0_OFFSET UINT32_C(0x0000023c)
#define AN7581_PPE_BIND_AGE1_OFFSET UINT32_C(0x00000240)
#define AN7581_PPE_HASH_SEED_OFFSET UINT32_C(0x00000244)
#define AN7581_PPE_DEFAULT_CPU_PORT_OFFSET UINT32_C(0x00000248)
#define AN7581_PPE_TABLE_HASH_CONFIG_OFFSET UINT32_C(0x00000250)
#define AN7581_PPE_TPID_CONFIG_OFFSET UINT32_C(0x00000288)
#define AN7581_PPE_TPID_ENABLE_OFFSET UINT32_C(0x0000028c)
#define AN7581_PPE_TPID_VALUE_OFFSET UINT32_C(0x000002d0)
#define AN7581_PPE_POLICY_OFFSET UINT32_C(0x00000304)
#define AN7581_PPE_RAM_CONTROL_OFFSET UINT32_C(0x0000031c)
#define AN7581_PPE_RAM_DATA_OFFSET UINT32_C(0x00000320)
#define AN7581_PPE_LEGACY_ENABLE_OFFSET UINT32_C(0x00000334)

#define AN7581_CHIP_ID_REGISTER UINT32_C(0x1fb00064)
#define AN7581_ETHERTYPE_REGISTER AN7581_FE_BASE
#define AN7581_GDM_TPID_CONTROL UINT32_C(0x1fb50280)
#define AN7581_GDM_TPID_ENABLE UINT32_C(0x1fb50284)
#define AN7581_GDM_TPID_VALUE0 UINT32_C(0x1fb50290)
#define AN7581_GDM_TPID_VALUE1 UINT32_C(0x1fb50294)
#define AN7581_GDM_TPID_VALUE2 UINT32_C(0x1fb50298)
#define AN7581_PACKET_POLICY_REGISTER UINT32_C(0x1fb50514)
#define AN7581_QDMA_MAPPING0 UINT32_C(0x1fb50500)
#define AN7581_QDMA_MAPPING1 UINT32_C(0x1fb51500)
#define AN7581_QDMA_MAPPING2 UINT32_C(0x1fb52500)
#define AN7581_CHIP_ID_SHIFT 16U
#define AN7581_EN7581_CHIP_ID UINT32_C(14)
#define AN7581_PPE_ENABLE UINT32_C(1)

#define AN7581_PPE_GLOBAL_ENABLE UINT32_C(0x00000001)
#define AN7581_PPE_GLOBAL_INITIAL_FEATURES UINT32_C(0x00000342)
#define AN7581_PPE_GLOBAL_FEATURE_CLEAR UINT32_C(0x0000003c)
#define AN7581_PPE_GLOBAL_SRAM_ENABLE UINT32_C(0x00008000)
#define AN7581_PPE_GLOBAL_TABLE_FEATURES UINT32_C(0x00030000)

#define AN7581_PPE_TABLE_ENTRY_COUNT_MASK UINT32_C(0x07000000)
#define AN7581_PPE_TABLE_PPE1_ENTRY_COUNT UINT32_C(0x04000000)
#define AN7581_PPE_TABLE_PPE2_ENTRY_COUNT UINT32_C(0x08000000)
#define AN7581_PPE_TABLE_INITIAL_CONFIG UINT32_C(0x00003000)
#define AN7581_PPE_TABLE_AGE_ENABLE UINT32_C(0x00000f80)
#define AN7581_PPE_TABLE_SEARCH_CONFIG UINT32_C(0x00000030)
#define AN7581_PPE_TABLE_DRAM_COUNT_MASK UINT32_C(0x00000007)

#define AN7581_PPE_FLOW_IPV6_HASH UINT32_C(0x00100000)
#define AN7581_PPE_FLOW_PPE2_HASH UINT32_C(0x00080000)
#define AN7581_PPE_FLOW_FINAL_CLEAR UINT32_C(0x000200c0)

#define AN7581_PPE_FLOW_TYPE3_MT7996 UINT32_C(0x06a0f7c0)

#define AN7581_PPE_HASH_SEED UINT32_C(0x12345678)
#define AN7581_PPE_TPID_ENABLE_DEFAULT UINT32_C(0x001d077f)
#define AN7581_PPE_TPID_VLAN UINT32_C(0x00008100)
#define AN7581_PPE_TPID_SERVICE_VLAN UINT32_C(0x000088a8)
#define AN7581_PPE_TPID_IPV4 UINT32_C(0x00000800)
#define AN7581_PPE_TPID_IPV6 UINT32_C(0x000086dd)

#define AN7581_PPE_SRAM_COUNT_FIELD UINT32_C(0x04000000)
#define AN7581_PPE_QDMA_BASE_CONFIG UINT32_C(0x00000101)
#define AN7581_PPE_QDMA_FINAL_CONFIG UINT32_C(0x30001000)
#define AN7581_PPE_TABLE_DRAM_COUNT_MT7996 UINT32_C(0x00000006)

#define AN7581_PPE_IP_CHECK_BLACKLIST UINT32_C(0x000f000f)
#define AN7581_PPE_IP_FILTER0_BLACKLIST UINT32_C(0x04291106)
#define AN7581_PPE_IP_FILTER1_BLACKLIST UINT32_C(0x00003a01)

#define AN7581_PPE_BIND_AGE0_MT7996 UINT32_C(0x000f003c)
#define AN7581_PPE_BIND_AGE1_MT7996 UINT32_C(0x0005003c)

#define AN7581_PPE_SRAM_CONTROL_ACK (UINT32_C(1) << 31)
#define AN7581_PPE_SRAM_CONTROL_ENTRY_MASK UINT32_C(0x00ffff00)
#define AN7581_PPE_SRAM_CONTROL_WRITE UINT32_C(2)
#define AN7581_PPE_SRAM_CONTROL_REQUEST UINT32_C(1)
#define AN7581_PPE2_FIRST_ENTRY UINT32_C(0x00002000)
#ifdef AN7581_MMIO_TEST
uint32_t an7581_ppe_test_dma_read32(uint32_t address);
#endif

static uint32_t ppe_base(bool ppe2) {
  return AN7581_FE_BASE + (ppe2 ? AN7581_PPE2_OFFSET : AN7581_PPE1_OFFSET);
}

static void ppe_update_bits(uint32_t address, uint32_t mask, uint32_t value) {
  uint32_t current = an7581_mmio_read32(address);

  an7581_mmio_write32(address, (current & ~mask) | (value & mask));
}

static void ppe_set_bits(uint32_t address, uint32_t bits) {
  ppe_update_bits(address, bits, bits);
}

static void ppe_clear_bits(uint32_t address, uint32_t bits) {
  ppe_update_bits(address, bits, 0U);
}

static void ppe_write_instances(uint32_t offset, uint32_t value) {
  bool ppe2;

  for (ppe2 = false;; ppe2 = true) {
    an7581_mmio_write32(ppe_base(ppe2) + offset, value);
    if (ppe2)
      break;
  }
}

static void ppe_update_instances(uint32_t offset, uint32_t mask,
                                 uint32_t value) {
  bool ppe2;

  for (ppe2 = false;; ppe2 = true) {
    ppe_update_bits(ppe_base(ppe2) + offset, mask, value);
    if (ppe2)
      break;
  }
}

static uint32_t ppe_chip_id(void) {
  return an7581_mmio_read32(AN7581_CHIP_ID_REGISTER) >> AN7581_CHIP_ID_SHIFT;
}

static uint32_t ppe_dma_read32(uint32_t address) {
#ifdef AN7581_MMIO_TEST
  return an7581_ppe_test_dma_read32(address);
#else
  uint32_t value = *(volatile const uint32_t *)(uintptr_t)address;

  an7581_dma_memory_barrier();
  return value;
#endif
}

static bool ppe2_is_available(void) {
  return ppe_chip_id() == AN7581_EN7581_CHIP_ID &&
         (an7581_mmio_read32(ppe_base(true) + AN7581_PPE_GLOBAL_CONFIG_OFFSET) &
          AN7581_PPE_ENABLE) != 0U;
}

static void ppe_configure_protocol_filter(void) {
  uint32_t filter;
  bool ppe2;

  for (ppe2 = false;; ppe2 = true) {
    uint32_t base = ppe_base(ppe2);

    for (filter = AN7581_PPE_IP_PROTOCOL_FILTER0_OFFSET;
         filter <= AN7581_PPE_IP_PROTOCOL_FILTER3_OFFSET;
         filter += sizeof(uint32_t))
      an7581_mmio_write32(base + filter, 0U);

    an7581_mmio_write32(base + AN7581_PPE_IP_PROTOCOL_CHECK_OFFSET,
                        AN7581_PPE_IP_CHECK_BLACKLIST);
    ppe_set_bits(base + AN7581_PPE_FLOW_CONFIG_OFFSET, UINT32_C(0x00010000));
    ppe_update_bits(base + AN7581_PPE_IP_PROTOCOL_FILTER0_OFFSET,
                    UINT32_C(0xffffffff), AN7581_PPE_IP_FILTER0_BLACKLIST);
    ppe_update_bits(base + AN7581_PPE_IP_PROTOCOL_FILTER1_OFFSET,
                    UINT32_C(0x0000ffff), AN7581_PPE_IP_FILTER1_BLACKLIST);

    if (ppe2)
      break;
  }
}

static void ppe_configure_tpid_entry(uint32_t index, bool enabled,
                                     uint32_t value) {
  uint32_t enable_bit = UINT32_C(1) << index;
  uint32_t value_shift = (index & 1U) * 16U;
  uint32_t value_mask = UINT32_C(0x0000ffff) << value_shift;
  uint32_t value_offset =
      AN7581_PPE_TPID_VALUE_OFFSET + ((index >> 1) * sizeof(uint32_t));
  bool ppe2;

  for (ppe2 = false;; ppe2 = true) {
    uint32_t base = ppe_base(ppe2);

    ppe_update_bits(base + AN7581_PPE_TPID_ENABLE_OFFSET, enable_bit,
                    enabled ? enable_bit : 0U);
    ppe_update_bits(base + value_offset, value_mask, value << value_shift);
    if (ppe2)
      break;
  }
}

static void
ppe_configure_tpid(const struct npu_ppe_initialize_request *request) {
  uint32_t ethertype = an7581_mmio_read32(AN7581_ETHERTYPE_REGISTER) >> 16U;

  ppe_write_instances(AN7581_PPE_TPID_CONFIG_OFFSET,
                      AN7581_PPE_TPID_ENABLE_DEFAULT);
  ppe_configure_tpid_entry(0U, true, AN7581_PPE_TPID_VLAN);
  ppe_configure_tpid_entry(1U, true, AN7581_PPE_TPID_SERVICE_VLAN);
  if (ethertype != AN7581_PPE_TPID_VLAN &&
      ethertype != AN7581_PPE_TPID_SERVICE_VLAN)
    ppe_configure_tpid_entry(2U, true, ethertype);

  ppe_configure_tpid_entry(3U, request->cds == 0U, AN7581_PPE_TPID_IPV4);
  ppe_configure_tpid_entry(4U, request->cds == 0U, AN7581_PPE_TPID_IPV6);
  if (request->cds != 0U || request->xpon_hal_api != 0U) {
    ppe_configure_tpid_entry(3U, false, AN7581_PPE_TPID_IPV4);
    ppe_configure_tpid_entry(4U, false, AN7581_PPE_TPID_IPV6);
  }
}

static void
ppe_configure_policy(const struct npu_ppe_initialize_request *request) {
  ppe_write_instances(AN7581_PPE_POLICY_OFFSET, UINT32_C(0x07d407d0));
  ppe_write_instances(AN7581_PPE_POLICY_OFFSET + UINT32_C(4),
                      UINT32_C(0x07dc07d8));
  ppe_write_instances(AN7581_PPE_POLICY_OFFSET + UINT32_C(8),
                      UINT32_C(0x07e407e0));
  ppe_write_instances(AN7581_PPE_POLICY_OFFSET + UINT32_C(12),
                      UINT32_C(0x000007e8));
  ppe_update_bits(AN7581_PACKET_POLICY_REGISTER, UINT32_C(0xffff0000),
                  request->max_packet != 0U ? UINT32_C(0x0fa40000)
                                            : UINT32_C(0x06a40000));
}

static void
ppe_configure_flow_type(const struct npu_ppe_initialize_request *request) {
  uint32_t flow_config;

  if (request->ppe_type == 1U)
    flow_config = UINT32_C(0x00008000);
  else if (request->ppe_type == 3U)
    flow_config = AN7581_PPE_FLOW_TYPE3_MT7996;
  else
    flow_config = UINT32_C(0x0620b0c0);

  ppe_update_instances(AN7581_PPE_FLOW_CONFIG_OFFSET, UINT32_C(0xfffeffff),
                       flow_config);
}

static void ppe_configure_table_hash(bool ppe2_was_enabled) {
  uint32_t table_count = ppe2_was_enabled ? UINT32_C(0x03000000)
                                          : AN7581_PPE_TABLE_PPE1_ENTRY_COUNT;
  bool ppe2;

  ppe_update_instances(AN7581_PPE_TABLE_HASH_CONFIG_OFFSET,
                       UINT32_C(0x00010101), AN7581_PPE_QDMA_BASE_CONFIG);

  ppe_update_bits(ppe_base(false) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                  AN7581_PPE_TABLE_ENTRY_COUNT_MASK, table_count);
  if (ppe2_was_enabled)
    ppe_update_bits(ppe_base(true) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                    AN7581_PPE_TABLE_ENTRY_COUNT_MASK, table_count);

  ppe_update_bits(ppe_base(false) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                  AN7581_PPE_TABLE_DRAM_COUNT_MASK,
                  AN7581_PPE_TABLE_DRAM_COUNT_MT7996);

  for (ppe2 = false;; ppe2 = true) {
    uint32_t hash_config = ppe_base(ppe2) + AN7581_PPE_TABLE_HASH_CONFIG_OFFSET;

    ppe_update_bits(hash_config, UINT32_C(0x000000f0), 0U);
    ppe_set_bits(hash_config, UINT32_C(0x00001000));
    ppe_update_bits(hash_config, UINT32_C(0x00f00000), 0U);
    if (!ppe2 || ppe2_was_enabled)
      ppe_update_bits(hash_config, UINT32_C(0xf0000000), UINT32_C(0x30000000));
    an7581_mmio_write32(ppe_base(ppe2) + AN7581_PPE_HASH_SEED_OFFSET,
                        AN7581_PPE_HASH_SEED);
    ppe_clear_bits(ppe_base(ppe2) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                   UINT32_C(0x00000008));
    ppe_set_bits(ppe_base(ppe2) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                 AN7581_PPE_TABLE_SEARCH_CONFIG);

    if (ppe2)
      break;
  }
}

static void ppe_configure_aging(void) {
  bool ppe2;

  for (ppe2 = false;; ppe2 = true) {
    uint32_t base = ppe_base(ppe2);

    ppe_set_bits(base + AN7581_PPE_TABLE_CONFIG_OFFSET,
                 AN7581_PPE_TABLE_AGE_ENABLE | AN7581_PPE_TABLE_INITIAL_CONFIG);
    ppe_update_bits(base + AN7581_PPE_UNBIND_AGE_OFFSET, UINT32_C(0xffff00ff),
                    UINT32_C(0x03e80003));
    ppe_update_bits(base + AN7581_PPE_BIND_AGE0_OFFSET, UINT32_C(0x7fff7fff),
                    AN7581_PPE_BIND_AGE0_MT7996);
    ppe_update_bits(base + AN7581_PPE_BIND_AGE1_OFFSET, UINT32_C(0x7fff7fff),
                    AN7581_PPE_BIND_AGE1_MT7996);
    an7581_mmio_write32(base + AN7581_PPE_KEEPALIVE_OFFSET,
                        UINT32_C(0x01010001));
    ppe_update_bits(base + AN7581_PPE_BIND_LIMIT0_OFFSET, UINT32_C(0x3fff3fff),
                    UINT32_C(0x0fa00fa0));
    ppe_update_bits(base + AN7581_PPE_BIND_LIMIT1_OFFSET, UINT32_C(0x00ff3fff),
                    UINT32_C(0x00011f40));
    an7581_mmio_write32(base + AN7581_PPE_BIND_RATE_OFFSET,
                        UINT32_C(0x001e001e));

    if (ppe2)
      break;
  }
}

static void ppe_configure_global_enable(void) {
  bool ppe2;

  for (ppe2 = false;; ppe2 = true) {
    uint32_t base = ppe_base(ppe2);

    ppe_update_bits(base + AN7581_PPE_GLOBAL_CONFIG_OFFSET,
                    AN7581_PPE_GLOBAL_FEATURE_CLEAR |
                        AN7581_PPE_GLOBAL_INITIAL_FEATURES,
                    AN7581_PPE_GLOBAL_INITIAL_FEATURES);
    ppe_set_bits(base + AN7581_PPE_GLOBAL_CONFIG_OFFSET,
                 AN7581_PPE_GLOBAL_SRAM_ENABLE);
    ppe_set_bits(base + AN7581_PPE_GLOBAL_CONFIG_OFFSET,
                 AN7581_PPE_GLOBAL_ENABLE);
    ppe_set_bits(base + AN7581_PPE_FLOW_CONFIG_OFFSET,
                 AN7581_PPE_FLOW_IPV6_HASH |
                     (ppe2 ? AN7581_PPE_FLOW_PPE2_HASH : 0U));
    ppe_set_bits(base + AN7581_PPE_GLOBAL_CONFIG_OFFSET,
                 AN7581_PPE_GLOBAL_TABLE_FEATURES);
    ppe_update_bits(base + AN7581_PPE_FLOW_CONFIG_OFFSET,
                    AN7581_PPE_FLOW_FINAL_CLEAR | AN7581_PPE_FLOW_PPE2_HASH,
                    AN7581_PPE_FLOW_PPE2_HASH);

    if (ppe2)
      break;
  }
}

static void
ppe_configure_queue_mapping(const struct npu_ppe_initialize_request *request,
                            bool enable) {
  uint32_t mapping = enable ? UINT32_C(0x00004444) : 0U;

  ppe_update_bits(AN7581_QDMA_MAPPING0, UINT32_C(0x0000ffff), mapping);
  if (request->wan_mode != 1U && request->wan_xsi == 0U &&
      request->wan_selection == 0U)
    ppe_update_bits(AN7581_QDMA_MAPPING1, UINT32_C(0x0000ffff),
                    enable ? UINT32_C(0x00008888) : UINT32_C(0x00005555));
  ppe_update_bits(AN7581_QDMA_MAPPING2, UINT32_C(0x0000ffff), mapping);
  ppe_write_instances(AN7581_PPE_DEFAULT_CPU_PORT_OFFSET,
                      request->wan_xsi == 0U ? UINT32_C(0x00000500)
                                             : UINT32_C(0x00055500));
}

static bool ppe_initialize(void *context,
                           const struct npu_ppe_initialize_request *request) {
  struct an7581_ppe_runtime *runtime = context;
  bool ppe2_was_enabled;

  if (runtime == NULL || request == NULL ||
      ppe_chip_id() != AN7581_EN7581_CHIP_ID)
    return false;

  (void)npu_memcpy(&runtime->configuration, request,
                   sizeof(runtime->configuration));
  ppe_set_bits(ppe_base(false) + AN7581_PPE_GLOBAL_CONFIG_OFFSET,
               UINT32_C(0x00000040));
  ppe_update_bits(ppe_base(false) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                  UINT32_C(0x10003000), AN7581_PPE_TABLE_INITIAL_CONFIG);
  ppe_set_bits(ppe_base(true) + AN7581_PPE_GLOBAL_CONFIG_OFFSET,
               UINT32_C(0x00000040));
  ppe_update_bits(
      ppe_base(true) + AN7581_PPE_TABLE_CONFIG_OFFSET, UINT32_C(0x18003000),
      AN7581_PPE_TABLE_PPE2_ENTRY_COUNT | AN7581_PPE_TABLE_INITIAL_CONFIG);
  ppe2_was_enabled = ppe2_is_available();

  ppe_configure_protocol_filter();
  ppe_configure_tpid(request);
  ppe_configure_policy(request);
  ppe_configure_flow_type(request);
  ppe_configure_table_hash(ppe2_was_enabled);
  ppe_configure_aging();
  ppe_configure_global_enable();
  ppe_configure_queue_mapping(request, true);
  runtime->initialized = true;
  return true;
}

static bool ppe_deinitialize(void *context) {
  struct an7581_ppe_runtime *runtime = context;
  bool ppe2;

  if (runtime == NULL || ppe_chip_id() != AN7581_EN7581_CHIP_ID)
    return false;

  ppe_configure_queue_mapping(&runtime->configuration, false);
  for (ppe2 = false;; ppe2 = true) {
    uint32_t global_config = ppe_base(ppe2) + AN7581_PPE_GLOBAL_CONFIG_OFFSET;

    ppe_clear_bits(global_config, AN7581_PPE_GLOBAL_ENABLE);
    ppe_clear_bits(global_config, AN7581_PPE_GLOBAL_INITIAL_FEATURES);
    ppe_clear_bits(global_config, AN7581_PPE_GLOBAL_SRAM_ENABLE);
    ppe_clear_bits(global_config, UINT32_C(0x00000010));
    ppe_set_bits(global_config, UINT32_C(0x0000000c));
    ppe_clear_bits(global_config, AN7581_PPE_GLOBAL_TABLE_FEATURES);
    if (ppe2)
      break;
  }

  an7581_mmio_write32(
      ppe_base(false) + AN7581_PPE_FLOW_CONFIG_OFFSET,
      an7581_mmio_read32(ppe_base(false) + AN7581_PPE_FLOW_CONFIG_OFFSET) &
          UINT32_C(0x00010000));
  an7581_mmio_write32(
      ppe_base(true) + AN7581_PPE_FLOW_CONFIG_OFFSET,
      an7581_mmio_read32(ppe_base(false) + AN7581_PPE_FLOW_CONFIG_OFFSET));
  ppe_clear_bits(ppe_base(false) + AN7581_PPE_TABLE_CONFIG_OFFSET,
                 AN7581_PPE_TABLE_AGE_ENABLE);
  runtime->initialized = false;
  return true;
}

static bool ppe_entry_uses_second_instance(uint32_t entry) {
  return entry >= AN7581_PPE2_FIRST_ENTRY && ppe2_is_available();
}

static uint32_t ppe_sram_command(uint32_t entry) {
  return ((entry << 8) & AN7581_PPE_SRAM_CONTROL_ENTRY_MASK) |
         AN7581_PPE_SRAM_CONTROL_WRITE | AN7581_PPE_SRAM_CONTROL_REQUEST;
}

static void ppe_wait_for_completion(uint32_t control_address) {
  uint32_t attempt;

  for (attempt = 0U; attempt < AN7581_PPE_ACK_POLL_ATTEMPTS; ++attempt) {
    if ((an7581_mmio_read32(control_address) & AN7581_PPE_SRAM_CONTROL_ACK) !=
        0U)
      return;
  }
}

static bool ppe_set_sram_entry(void *context, bool ppe2, uint32_t dma_address,
                               uint32_t size) {
  uint32_t local_address;
  uint32_t data_address;
  uint32_t offset;

  (void)context;
  if (size != NPU_PPE_SRAM_ENTRY_SIZE ||
      !an7581_dma_buffer_map(dma_address, size, sizeof(uint32_t),
                             &local_address))
    return false;

  data_address = ppe_base(ppe2) + AN7581_PPE_RAM_DATA_OFFSET;
  an7581_dma_memory_barrier();
  for (offset = 0U; offset < NPU_PPE_SRAM_ENTRY_SIZE;
       offset += sizeof(uint32_t))
    an7581_mmio_write32(data_address + offset,
                        ppe_dma_read32(local_address + offset));
  an7581_memory_barrier();
  return true;
}

static bool ppe_commit_sram_entry(void *context, uint32_t entry,
                                  uint32_t size) {
  uint32_t control_address;

  (void)context;
  if (size != sizeof(uint32_t) || entry >= NPU_PPE_SRAM_ENTRY_COUNT)
    return false;

  control_address = ppe_base(ppe_entry_uses_second_instance(entry)) +
                    AN7581_PPE_RAM_CONTROL_OFFSET;
  an7581_mmio_write32(control_address, ppe_sram_command(entry));
  ppe_wait_for_completion(control_address);
  return true;
}

static bool ppe_reset_sram_entries(void *context, uint32_t entry_count) {
  uint32_t entry;

  (void)context;
  if (entry_count != NPU_PPE_SRAM_ENTRY_COUNT)
    return false;

  for (entry = 0U; entry < entry_count; ++entry) {
    uint32_t base = ppe_base(ppe_entry_uses_second_instance(entry));
    uint32_t control_address = base + AN7581_PPE_RAM_CONTROL_OFFSET;

    an7581_mmio_write32(base + AN7581_PPE_RAM_DATA_OFFSET, 0U);
    an7581_mmio_write32(control_address, ppe_sram_command(entry));
    ppe_wait_for_completion(control_address);
  }

  return true;
}

static const struct npu_ppe_backend_operations g_ppe_backend = {
    .initialize = ppe_initialize,
    .deinitialize = ppe_deinitialize,
    .set_sram_entry = ppe_set_sram_entry,
    .commit_sram_entry = ppe_commit_sram_entry,
    .reset_sram_entries = ppe_reset_sram_entries,
};

bool an7581_ppe_runtime_reset(struct an7581_ppe_runtime *runtime) {
  if (runtime == NULL)
    return false;

  (void)npu_memset(runtime, 0U, sizeof(*runtime));
  return true;
}

const struct npu_ppe_backend_operations *an7581_ppe_backend_operations(void) {
  return &g_ppe_backend;
}

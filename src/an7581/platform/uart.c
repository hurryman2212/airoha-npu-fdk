/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/platform/uart.h"

#include "an7581/platform/memory_map.h"
#include "an7581/platform/mmio.h"
#include "an7581/platform/plic.h"

static uint8_t uart_read8(uint32_t offset) {
#ifdef AN7581_MMIO_TEST
  return (uint8_t)an7581_mmio_read32(AN7581_NPU_UART_BASE + offset);
#else
  uint8_t value =
      *(const volatile uint8_t *)(uintptr_t)(AN7581_NPU_UART_BASE + offset);

  an7581_memory_barrier();
  return value;
#endif
}

static void uart_echo_interrupt_handler(uint32_t source, void *context) {
  uint8_t received = 0U;

  (void)source;
  (void)context;
  if ((uart_read8(AN7581_NPU_UART_LINE_STATUS_OFFSET) &
       AN7581_UART_LINE_STATUS_DATA_READY) == 0U)
    return;

  do {
    received = uart_read8(AN7581_NPU_UART_DATA_OFFSET);
  } while ((uart_read8(AN7581_NPU_UART_LINE_STATUS_OFFSET) &
            AN7581_UART_LINE_STATUS_DATA_READY) != 0U);

  while ((an7581_mmio_read32(AN7581_NPU_UART_BASE +
                             AN7581_NPU_UART_LINE_STATUS_OFFSET) &
          AN7581_UART_LINE_STATUS_TRANSMIT_EMPTY) == 0U)
    an7581_cpu_relax();
  an7581_mmio_write32(AN7581_NPU_UART_BASE + AN7581_NPU_UART_DATA_OFFSET,
                      received);
}

void an7581_uart_configure(void) {
  an7581_mmio_write32(
      AN7581_NPU_UART_BASE + AN7581_NPU_UART_FIFO_CONTROL_OFFSET, 0x0fU);
  an7581_mmio_write32(
      AN7581_NPU_UART_BASE + AN7581_NPU_UART_MODEM_CONTROL_OFFSET, 0U);
  an7581_mmio_write32(AN7581_NPU_UART_BASE + AN7581_NPU_UART_HIGH_SPEED_OFFSET,
                      0U);
  an7581_mmio_write32(
      AN7581_NPU_UART_BASE + AN7581_NPU_UART_INTERRUPT_ENABLE_OFFSET, 1U);

  /*
   * Bit 7 selects the divisor-latch aliases at offsets 0x00 and 0x04.
   * Offset 0x2c receives the vendor baud-rate and input-clock configuration
   * word recovered from the MT7996 image.
   */
  an7581_mmio_write32(
      AN7581_NPU_UART_BASE + AN7581_NPU_UART_LINE_CONTROL_OFFSET, 0x80U);
  an7581_mmio_write32(AN7581_NPU_UART_BASE +
                          AN7581_NPU_UART_BAUD_CONFIGURATION_OFFSET,
                      UINT32_C(0xea00fde8));
  an7581_mmio_write32(AN7581_NPU_UART_BASE + AN7581_NPU_UART_DATA_OFFSET, 1U);
  an7581_mmio_write32(
      AN7581_NPU_UART_BASE + AN7581_NPU_UART_INTERRUPT_ENABLE_OFFSET, 0U);
  an7581_mmio_write32(
      AN7581_NPU_UART_BASE + AN7581_NPU_UART_LINE_CONTROL_OFFSET, 3U);
}

bool an7581_uart_interrupt_initialize(void) {
  if (!an7581_plic_register_handler(AN7581_UART_INTERRUPT_SOURCE,
                                    uart_echo_interrupt_handler, NULL))
    return false;
  if (an7581_plic_set_enabled(AN7581_UART_INTERRUPT_SOURCE, true))
    return true;

  (void)an7581_plic_unregister_handler(AN7581_UART_INTERRUPT_SOURCE,
                                       uart_echo_interrupt_handler, NULL);
  return false;
}

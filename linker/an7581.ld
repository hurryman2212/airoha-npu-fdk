/* SPDX-License-Identifier: GPL-2.0-only */
OUTPUT_ARCH(riscv)
ENTRY(_start)

FIRMWARE_BASE = 0x84000000;
FIRMWARE_MAX_SIZE = 0x00200000;
DATA_BASE = 0x3e900000;
DATA_MAX_SIZE = 0x00010000;
STACK_REGION_BASE = 0x8401de00;
STACK_SIZE = 0x4000;
CORE_COUNT = 8;
CRASH_DUMP_REGION_SIZE = 0x1000;
CRASH_DUMP_BASE = FIRMWARE_BASE + FIRMWARE_MAX_SIZE - CRASH_DUMP_REGION_SIZE;

MEMORY
{
	FIRMWARE (rwx) : ORIGIN = FIRMWARE_BASE, LENGTH = FIRMWARE_MAX_SIZE
	LOCAL_DATA (rw) : ORIGIN = DATA_BASE, LENGTH = DATA_MAX_SIZE
}

SECTIONS
{
	. = ORIGIN(FIRMWARE);

	.init : ALIGN(4)
	{
		KEEP(*(.init))
	} > FIRMWARE

	.text : ALIGN(4)
	{
		*(.text .text.*)
	} > FIRMWARE

	.rodata : ALIGN(4)
	{
		KEEP(*(.rodata.firmware_version))
		*(.rodata .rodata.*)
	} > FIRMWARE
	__firmware_payload_end = .;

	.data ORIGIN(LOCAL_DATA) : ALIGN(4)
	{
		__data_start = .;
		KEEP(*(.data.platform))
		*(.data.boot)
		*(.data .data.*)
		*(.sdata .sdata.*)
		__data_end = .;
	} > LOCAL_DATA

	__global_pointer$ = MIN(__data_start + 0x800,
			       MAX(__data_start + 0x800, __data_end - 0x800));

	.bss (NOLOAD) : ALIGN(4)
	{
		__bss_start = .;
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > LOCAL_DATA

	.stacks STACK_REGION_BASE (NOLOAD) : ALIGN(16)
	{
		__stack_bottom = .;
		. += STACK_SIZE * CORE_COUNT;
		__stack_top = .;
	} > FIRMWARE

	.crash_dump CRASH_DUMP_BASE (NOLOAD) : ALIGN(64)
	{
		__crash_dump_start = .;
		KEEP(*(.crash_dump))
		. = __crash_dump_start + CRASH_DUMP_REGION_SIZE;
		__crash_dump_end = .;
	} > FIRMWARE

	__firmware_end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame.*)
	}
}

ASSERT(__firmware_payload_end <= CRASH_DUMP_BASE,
       "firmware payload overlaps the crash dump region")
ASSERT(__firmware_payload_end <= STACK_REGION_BASE,
       "firmware payload overlaps the original MT7996 stack region")
ASSERT(__bss_end <= ORIGIN(LOCAL_DATA) + LENGTH(LOCAL_DATA),
       "initialized data and BSS exceed the 64 KiB local SRAM region")
ASSERT(__crash_dump_end - __crash_dump_start == CRASH_DUMP_REGION_SIZE,
       "crash dump region size changed")
ASSERT(__firmware_end <= ORIGIN(FIRMWARE) + LENGTH(FIRMWARE),
       "firmware exceeds the 2 MiB loader limit")

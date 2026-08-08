/* SPDX-License-Identifier: GPL-2.0-only */

	.section .init, "ax", @progbits
	.globl _start
	.type _start, @function
	.option push
	.option norelax
_start:
	csrwi	mie, 0
	li	t0, 8
	csrc	mstatus, t0

	csrr	a0, mhartid
	li	t0, 8
	bgeu	a0, t0, .Lpark

	la	gp, __global_pointer$
	la	sp, __stack_bottom
	addi	t0, a0, 1
	slli	t0, t0, 14
	add	sp, sp, t0

	la	t0, trap_entry
	csrw	mtvec, t0

	/* The AN7581 cores do not implement the RISC-V A extension.  Hart 0 is
	 * always enabled by the host boot mask, so it owns the one-time BSS setup
	 * while the other harts wait on an ordinary local-SRAM word.
	 */
	bnez	a0, .Lwait_for_bss

	la	t0, __bss_start
	la	t1, __bss_end
.Lclear_bss:
	bgeu	t0, t1, .Lbss_cleared
	sw	zero, 0(t0)
	addi	t0, t0, 4
	j	.Lclear_bss

.Lbss_cleared:
	call	an7581_crash_dump_initialize
	fence	rw, rw
	la	t0, g_boot_pending
	sw	zero, 0(t0)
	j	.Lrun

.Lwait_for_bss:
	la	t0, g_boot_pending
.Lwait:
	lw	t1, 0(t0)
	bnez	t1, .Lwait
	fence	rw, rw

.Lrun:
	csrr	a0, mhartid
	call	firmware_main

.Lpark:
	wfi
	j	.Lpark
	.size _start, . - _start
	.option pop

	.section .data.boot, "aw", @progbits
	.balign 4
	.globl g_boot_pending
	.type g_boot_pending, @object
	.size g_boot_pending, 4
g_boot_pending:
	.word 1

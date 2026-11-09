/* Patched call site: LapisPlatformExpert::start+0x60 `mov x0, x20` (return value)
 * -> `bl drv_pe_hook`.  x19 = this (LapisPlatformExpert), x20 = super::start() result. */
	.text
	.globl drv_pe_hook
	.type drv_pe_hook, %function
drv_pe_hook:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	mov	x0, x19
	bl	lapis_drivers_start
	ldp	x29, x30, [sp], #16
	mov	x0, x20
	ret

/* drv_iolog_str(const char *s): IOLog("%s", s) with the Darwin variadic convention
 * (first variadic argument in the stack slot at [sp] at the call). */
	.globl drv_iolog_str
	.type drv_iolog_str, %function
drv_iolog_str:
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]
	add	x29, sp, #16
	str	x0, [sp]
	adr	x0, 1f
	bl	IOLog
	ldp	x29, x30, [sp, #16]
	add	sp, sp, #32
	ret
1:	.asciz "%s"
	.balign 4

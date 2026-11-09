/* lapis aux blob: lives in the NOP padding after the exception vector table
 * (0xfffffe000756a800..0x756b000, __TEXT_EXEC, read+exec). Source equivalent:
 * patch 0017 (bsd/pthread/lapis_pthread_stub.c). */
	.text
	.global _astart
_astart:
/* s10: built-in stand-in for pthread.kext's function table, so bsd_init's
 * pthread_init() works without a kernelcache. Process/psynch syscalls return
 * ENOTSUP until the real (open-source libpthread kern/) table is built in.
 * Layout = struct pthread_functions_s, xnu-12377 bsd/sys/pthread_shims.h. */
	.balign 8
	.global lapis_pthread_functions
lapis_pthread_functions:
	.quad	1			/* version (int + pad) */
	.quad	pt_ret			/* 0x08 pthread_init */
	.quad	0,0,0,0,0,0		/* 0x10..0x3f unused */
	.quad	pt_ret			/* 0x40 pth_proc_hashinit */
	.quad	pt_ret			/* 0x48 pth_proc_hashdelete */
	.quad	pt_enotsup		/* 0x50 bsdthread_create */
	.quad	pt_enotsup		/* 0x58 bsdthread_register */
	.quad	pt_enotsup		/* 0x60 bsdthread_terminate */
	.quad	pt_enotsup		/* 0x68 thread_selfid */
	.quad	0,0			/* 0x70,0x78 */
	.rept 11			/* 0x80..0xd7 psynch_* */
	.quad	pt_enotsup
	.endr
	.quad	0			/* 0xd8 */
	.quad	pt_enotsup		/* 0xe0 bsdthread_register2 */
	.quad	0,0,0,0,0,0,0		/* 0xe8..0x11f */
	.quad	0			/* 0x120 pthread_find_owner (NULL-checked) */
	.quad	0			/* 0x128 pthread_get_thread_kwq (NULL-checked) */
	.quad	0			/* 0x130 */
	.quad	pt_enotsup		/* 0x138 workq_handle_stack_events */
	.quad	pt_enotsup		/* 0x140 workq_create_threadstack */
	.quad	pt_enotsup		/* 0x148 workq_destroy_threadstack */
	.quad	pt_ret			/* 0x150 workq_setup_thread */
	.quad	pt_ret			/* 0x158 workq_markfree_threadstack */
	.fill	83, 8, 0		/* _pad[83] */
lapis_pthread_functions_end:
	.if (lapis_pthread_functions_end - lapis_pthread_functions) != 0x160 + 83*8
	.error "pthread_functions_s layout"
	.endif
pt_ret:
	ret
pt_enotsup:
	mov	w0, #45			/* ENOTSUP */
	ret
	.global _aend
_aend:

/* HISTORICAL PROTOTYPE ONLY — NOT BUILT OR INJECTED.
 * This old pthread_functions stand-in contained no-op init/hash/workq
 * callbacks and must not be used. The matching pthread.kext must be loaded
 * through a verified kernel collection instead. */
	.text
	.global _astart
_astart:
/* s10: built-in stand-in for pthread.kext's function table, so bsd_init's
 * pthread_init() works without a kernelcache. thread_selfid must work before
 * dyld initializes TSD; returning an errno here sends dyld's cerror path to
 * an uninitialized TPIDRRO_EL0. Other process/psynch calls still return ENOTSUP.
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
	.quad	pt_thread_selfid		/* 0x68 thread_selfid */
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

/* int thread_selfid(proc_t p, uint64_t *retval).
 * XNU's BSD wrapper moves its third argument (retval) to x1 before the call.
 * The callback returns an errno in w0; the ID belongs in *retval, not x0.
 * Both imports are kernel functions, with the ordinary fixed-argument ABI. */
	.balign 4
pt_thread_selfid:
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp
	str	x19, [sp, #16]
	mov	x19, x1
	bl	k_current_thread
	bl	k_thread_tid
	str	x0, [x19]
	mov	w0, #0
	ldr	x19, [sp, #16]
	ldp	x29, x30, [sp], #32
	ret
	.global _aend
_aend:

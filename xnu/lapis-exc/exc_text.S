/*
 * lapis early exception reporter (replaces the s6 bit-bar regdump).
 * Entered from the LowException-style stubs with x9 = vector slot (0..15).
 * Runs from its PHYSICAL address (VBAR is physical before arm_vm_init), turns
 * the MMU/D-cache off first, so every framebuffer row is reachable with no
 * page tables.  No stack, no memory writes except to the FB, aligned 32-bit
 * stores only (Device memory with MMU off).
 *
 * Screen:  rows 420..451  full-width band coloured by EC class (as s6):
 *            magenta = instruction abort (EC 0x20/0x21)
 *            yellow  = data abort       (EC 0x24/0x25)
 *            cyan    = unknown/sysreg   (EC 0x00/0x18)
 *            green-ish= other
 *          rows 456..  black box with WHITE 4x-scaled 8x16 hex text:
 *            EC xx V s        (s = vector slot, hex)
 *            ESR / ELR / FAR / SP / X30   16 hex digits each
 */
#define FB_BASE   0xD5100000
#define STRIDE    11776           /* 2944 px * 4 */
#define BAND_ROW  420
#define TEXT_ROW  460
#define TEXT_X    64
#define SCALE     4
#define ADV       36              /* 8*4 + 4 px spacing */
#define LINE_H    72              /* 16*4 + 8 */

	.text
	.global _start
_start:
	mrs	x10, sctlr_el1
	mov	x11, #5                 /* M | C */
	bic	x10, x10, x11
	msr	sctlr_el1, x10
	isb
	mrs	x13, esr_el1
	mrs	x14, elr_el1
	mrs	x15, far_el1
	mov	x16, sp
	mov	x17, x30
	lsr	x12, x13, #26
	and	x12, x12, #0x3f

	/* ---- colour band ---- */
	ldr	w1, =0x00ffa000          /* default: orange */
	cmp	x12, #0x20
	b.eq	1f
	cmp	x12, #0x21
	b.eq	1f
	cmp	x12, #0x24
	b.eq	2f
	cmp	x12, #0x25
	b.eq	2f
	cmp	x12, #0x00
	b.eq	3f
	cmp	x12, #0x18
	b.eq	3f
	b	4f
1:	ldr	w1, =0x00ff00ff
	b	4f
2:	ldr	w1, =0x00ffff00
	b	4f
3:	ldr	w1, =0x0000ffff
4:	ldr	x0, =(FB_BASE + BAND_ROW * STRIDE)
	ldr	x3, =(32 * STRIDE / 4)
5:	str	w1, [x0], #4
	subs	x3, x3, #1
	b.ne	5b
	/* ---- black text box: rows 452 .. 452+6*72+16, cols 0..800 ---- */
	mov	x20, #STRIDE
	ldr	x0, =(FB_BASE + 452 * STRIDE)
	mov	x2, #(6 * LINE_H + 16)
6:	mov	x5, x0
	mov	x3, #800
7:	str	wzr, [x5], #4
	subs	x3, x3, #1
	b.ne	7b
	add	x0, x0, x20
	subs	x2, x2, #1
	b.ne	6b

	adr	x19, font
	/* line 0: "EC xx V s" */
	mov	x22, #TEXT_ROW
	mov	x21, #TEXT_X
	mov	w0, #0xE ; bl putc
	mov	w0, #0xC ; bl putc
	add	x21, x21, #ADV
	mov	x23, x12 ; mov x24, #2 ; bl puthex
	add	x21, x21, #ADV
	mov	w0, #21 ; bl putc        /* V */
	add	x21, x21, #ADV
	mov	x23, x9 ; mov x24, #1 ; bl puthex

	.macro LINE l0, l1, l2, reg
	add	x22, x22, #LINE_H
	mov	x21, #TEXT_X
	mov	w0, #\l0 ; bl putc
	mov	w0, #\l1 ; bl putc
	mov	w0, #\l2 ; bl putc
	add	x21, x21, #ADV
	mov	x23, \reg ; mov x24, #16 ; bl puthex
	.endm
	LINE 0xE, 19, 18, x13      /* ESR */
	LINE 0xE, 16, 18, x14      /* ELR */
	LINE 0xF, 0xA, 18, x15     /* FAR */
	LINE 19, 17, 22, x16       /* SP_ */
	LINE 20, 3, 0, x17         /* X30 */
8:	wfe
	b	8b

/* puthex: x23 value, x24 digit count (msb first); uses putc; ret via x25 */
puthex:
	mov	x25, x30
	sub	x26, x24, #1
	lsl	x26, x26, #2
9:	lsr	x0, x23, x26
	and	w0, w0, #0xf
	bl	putc
	subs	x26, x26, #4
	b.ge	9b
	mov	x30, x25
	ret

/* putc: w0 glyph idx (22 = blank), x21 x-px (advanced), x22 top row.
 * clobbers x0-x8,x10,x11.  font rows are MSB = leftmost pixel. */
putc:
	cmp	w0, #22
	b.eq	99f
	add	x1, x19, x0, lsl #4      /* glyph */
	ldr	x2, =FB_BASE
	mov	x3, #STRIDE
	madd	x2, x22, x3, x2
	add	x2, x2, x21, lsl #2      /* top-left pixel */
	ldr	w10, =0x00ffffff
	mov	x4, #0                   /* glyph row */
10:	ldrb	w5, [x1, x4]
	mov	x6, #SCALE
11:	mov	x7, x2
	mov	x8, #7                   /* bit index, 7 = leftmost */
12:	lsr	w11, w5, w8
	tst	w11, #1
	csel	w11, w10, wzr, ne
	str	w11, [x7], #4
	str	w11, [x7], #4
	str	w11, [x7], #4
	str	w11, [x7], #4
	subs	x8, x8, #1
	b.ge	12b
	add	x2, x2, x20
	subs	x6, x6, #1
	b.ne	11b
	add	x4, x4, #1
	cmp	x4, #16
	b.ne	10b
99:	add	x21, x21, #ADV
	ret

	.ltorg
	.balign 8
font:
#include "glyphs.inc"

/*
 * prng fallback (source equivalent: xnu patch 0014, osfmk/prng/prng_random.c
 *   read_random(): if (!prng_ready) { read_erandom_generate(buf, n); return; }
 * ).  Without a kernelcache the corecrypto kext never calls
 * register_and_init_prng(), so prng_funcs.refresh == NULL and read_random()
 * jumped to PC 0 (seen in QEMU).  read_random's first instruction is patched
 * to "b prng_fallback".
 */

/*
 * corecrypto-less fallbacks (source equivalent: xnu patch 0014,
 * libkern/crypto/corecrypto_rand.c): with g_crypto_funcs == NULL (no
 * corecrypto kext without a kernelcache) the crypto_random_* wrappers use
 * the kernel's own early DRBG (read_erandom_generate) instead of jumping
 * through NULL.  First seen: vm_map_store_crypto_init (STARTUP EARLY_BOOT).
 * NOTE: the early DRBG has no lock; fine with cpus=1 (bring-up only).
 */
	.macro CRSTUB off, tmp
	adrp	x8, k_g_crypto_funcs
	ldr	x8, [x8, :lo12:k_g_crypto_funcs]
	cbz	x8, 1f
	ldr	\tmp, [x8, #\off]
	br	\tmp
1:
	.endm
	.balign 4
	.global cr_random_generate
cr_random_generate:                      /* (ctx, buf, size) */
	CRSTUB 320, x3
	mov	x0, x1
	mov	x1, x2
	b	k_read_erandom_generate
	.global cr_random_uniform
cr_random_uniform:                       /* (ctx, bound, uint64_t *out) */
	CRSTUB 328, x3
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp
	stp	x1, x2, [sp, #16]
	mov	x0, x2
	mov	x1, #8
	bl	k_read_erandom_generate
	ldp	x1, x2, [sp, #16]
	ldr	x3, [x2]
	cbz	x1, 2f
	udiv	x4, x3, x1
	msub	x3, x4, x1, x3
2:	str	x3, [x2]
	ldp	x29, x30, [sp], #32
	ret
	.global cr_random_kmem_ctx_size
cr_random_kmem_ctx_size:
	CRSTUB 336, x0
	mov	x0, #16
	ret
	.global cr_random_kmem_init
cr_random_kmem_init:
	CRSTUB 344, x1
	ret

/*
 * trust_cache_init() guard (source equivalent: xnu patch 0015,
 * bsd/kern/kern_trustcache.c): without AppleImage4 / AMFI kexts img4if and
 * amfi are NULL and trust_cache_runtime_init() panics "image4 interface not
 * available" (seen in QEMU).  Skip trust caches entirely in that case.
 */
	.global tc_init_guard
tc_init_guard:
	adrp	x8, k_img4if
	ldr	x8, [x8, :lo12:k_img4if]
	cbz	x8, 1f
	adrp	x8, k_amfi
	ldr	x8, [x8, :lo12:k_amfi]
	cbz	x8, 1f
	stp	x29, x30, [sp, #-16]!          /* displaced insn */
	b	k_trust_cache_init_body
1:	ret

	.org	0x838
	.global prng_fallback
prng_fallback:
	adrp	x8, k_prng_ready
	ldr	w8, [x8, :lo12:k_prng_ready]               /* prng_ready */
	cbz	w8, 1f
	stp	x26, x25, [sp, #-80]!          /* displaced read_random insn */
	b	k_read_random_body
1:	mov	w1, w1                         /* u_int numbytes -> size_t */
	b	k_read_erandom_generate             /* read_erandom_generate(buf, n) */

/* lapis built-in drivers blob, two text regions + one bss.
 *  TEXT  [0xfffffe00074fdb18, 0xfffffe0007507898) 40320 B: vm_range_lock_test_* bodies
 *  TEXT2 [0xfffffe0007509d90, 0xfffffe000751898c) 60412 B: vm_range_lock_test_preflight_* bodies
 *        Only callers: vm_range_lock_test / vm_range_lock_preflight_test (debug.test sysctl
 *        handlers), both patched to return ENOTSUP (tools/drv_binpatches.py).
 *  BSS   [0xfffffe0007daf000, 0xfffffe0007db0000): zero tail of __DATA (shim zero-fills).
 *        First 64 bytes = struct lapis_blkops at a FIXED address (read by lapisfs).
 * No .data allowed (nothing would load it). */
MEMORY {
	TEXT  (rx) : ORIGIN = 0xfffffe00074fdb18, LENGTH = 40320
	TEXT2 (rx) : ORIGIN = 0xfffffe0007509d90, LENGTH = 60412
	BSS   (rw) : ORIGIN = 0xfffffe0007daf000, LENGTH = 4096
}
SECTIONS {
	.text : { *(.text.entry) *drv_entry.o(.text*) *klog.o(.text* .rodata*) *drv_common.o(.text* .rodata*)
	          *lapis_gic.o(.text* .rodata*) *gic_selftest.o(.text* .rodata*) . = ALIGN(4); } > TEXT
	.text2 : { *lapis_ufs.o(.text* .rodata*) *lapis_blk.o(.text* .rodata*)
	           *lapis_fb.o(.text* .rodata*) *lapis_smp.o(.text* .rodata*) *lapis_sdhci.o(.text* .rodata*)
	           *lapis_touch.o(.text* .rodata*) *lapis_batt.o(.text* .rodata*)
	           *lapis_wifi.o(.text* .rodata*) *lapis_gpu.o(.text* .rodata*) *lapis_audio.o(.text* .rodata*) *lapis_crypter.o(.text* .rodata*) *qcom_scm.o(.text* .rodata*) *lapis_pil.o(.text* .rodata*)
	           *(.text*) *(.rodata*) *(.data.rel.ro*) . = ALIGN(4); } > TEXT2
	.data : { *(.data*) *(.sdata*) } > TEXT
	.bss (NOLOAD) : { *(.bss.blkops) *(.bss*) *(COMMON) } > BSS
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

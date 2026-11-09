/* lapis built-in drivers blob.
 *  .text/.rodata: over the sysctl-only vm_range_lock_test_* functions
 *      [0xfffffe00074fdb18, 0xfffffe0007507898) = 40320 bytes; their only callers
 *      (vm_range_lock_test) get an ENOTSUP entry patch (tools/drv_binpatches.py).
 *  .bss: zero-filled tail of the kernel __DATA segment after __common
 *      [0xfffffe0007daf000, 0xfffffe0007db0000) - mapped RW, zeroed by the shim
 *      (vmsize > filesize).  No .data allowed (nothing would load it). */
MEMORY {
	TEXT (rx) : ORIGIN = 0xfffffe00074fdb18, LENGTH = 40320
	BSS  (rw) : ORIGIN = 0xfffffe0007daf000, LENGTH = 4096
}
SECTIONS {
	.text : { *(.text.entry) *(.text*) *(.rodata*) *(.data.rel.ro*) . = ALIGN(4); } > TEXT
	.data : { *(.data*) *(.sdata*) } > TEXT
	.bss (NOLOAD) : { *(.bss*) *(COMMON) } > BSS
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

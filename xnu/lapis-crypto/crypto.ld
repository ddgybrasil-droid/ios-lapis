SECTIONS {
	. = 0xfffffe000743aa10;
	.text : { *(.text*) *(.rodata*) *(.data.rel.ro*) *(.data*) *(.bss*) *(COMMON) }
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

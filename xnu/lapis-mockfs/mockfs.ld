SECTIONS {
	. = 0xfffffe000743cac0;
	.text : {
		*(.text*)
		*(.rodata*)
		*(.data.rel.ro*)
		. = ALIGN(8);
		*(.data*)
		*(.bss*)
		*(COMMON)
	}
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

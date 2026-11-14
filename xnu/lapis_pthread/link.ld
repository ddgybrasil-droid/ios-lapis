SECTIONS {
  . = CAVE;
  .text : { *(.text.table) *(.text .text.*) *(.rodata .rodata.*) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
  .data : { *(.data .data.* .bss .bss.*) }
}

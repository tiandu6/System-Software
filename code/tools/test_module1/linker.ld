OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x80200000;
  .text : { *(.text.entry) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) *(.sdata .sdata.*) }
  . = ALIGN(16);
  __bss_start = .;
  .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
  . = ALIGN(16);
  __bss_end = .;
}

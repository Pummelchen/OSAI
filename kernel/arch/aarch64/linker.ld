ENTRY(kernel_entry)

SECTIONS
{
  . = 0x40200000;
  __kernel_start = .;

  .text : ALIGN(0x1000) {
    *(.text.entry)
    *(.text .text.*)
  }

  .rodata : ALIGN(0x1000) {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(0x1000) {
    *(.data .data.*)
  }

  .bss : ALIGN(0x1000) {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    __bss_end = .;
  }

  . = ALIGN(0x1000);
  __kernel_end = .;
}

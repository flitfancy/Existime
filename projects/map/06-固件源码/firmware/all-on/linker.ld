ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 16K
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 6K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .status (NOLOAD) : { KEEP(*(.status)) . = ALIGN(4); } > RAM
  .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT>FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  /DISCARD/ : { *(.comment) *(.note*) }
  ASSERT(_ebss < _estack - 1024, "Insufficient stack space")
  ASSERT(SIZEOF(.status) == 92, "Unexpected diagnostics layout")
}

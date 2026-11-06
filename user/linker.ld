ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : { *(.text._start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) *(.bss .bss.*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame*) }
}

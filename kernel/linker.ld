/* Limine ELF'i olduğu gibi yüklüyor, objcopy yok.
 * kernel en üst 2 GiB'ta, Makefile'daki -mcmodel=kernel bunun için */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

/* her section ayrı PT_LOAD, Limine izinleri buna göre veriyor (rx / r / rw) */
PHDRS
{
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;

    /* Limine request'leri bu aralıkta arıyor, kernel.c'ye bak.
     * KEEP: kimse referans vermiyor, linker atmasın */
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .text : { *(.text .text.*) } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .rodata : { *(.rodata .rodata.*) } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : { *(.data .data.*) } :data

    /* .bss dosyada yer kaplamıyor, Limine yüklerken sıfırlıyor */
    .bss : { *(.bss .bss.*) *(COMMON) } :data

    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame*) }
}

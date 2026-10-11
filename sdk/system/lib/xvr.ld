/* itelx programs: linked at 0x400000, one segment per protection
 * (text RX, rodata R, data+bss RW), each page aligned. tools/xvrlink turns
 * the result into an XVR file. */
OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x400000;
    .text : { *(.text.start) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}

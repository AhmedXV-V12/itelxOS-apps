/* xvcapp: system/lib/xvr.ld plus one more segment at the end, the payload
 * (read only). xvc2xvr puts an XVC program into it; it must stay the last
 * segment and the last bytes of the XVR file, so the program can grow
 * without moving anything else. */
OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);
    rodata  PT_LOAD FLAGS(4);
    data    PT_LOAD FLAGS(6);
    payload PT_LOAD FLAGS(4);
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
    . = ALIGN(4096);
    .xvcapp : { __xvcapp_start = .; KEEP(*(.xvcapp)) } :payload
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}

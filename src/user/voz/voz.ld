/* SOMA_241_VOZ: como user.ld (codigo R+X y datos R+W separados por pagina, nunca W+X) mas la tabla de constructores
 * globales de C++ (.init_array), que recorre voz_cstart. */
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);   /* R + X */
    data PT_LOAD FLAGS(6);   /* R + W */
}
SECTIONS
{
    . = 0x8000000000;
    .text   : { *(.text._start) *(.text .text.* .gnu.linkonce.t.*) } :text
    .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } :text
    . = ALIGN(4096);
    .init_array : { PROVIDE(__init_array_start = .); KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array .ctors)) PROVIDE(__init_array_end = .); } :data
    .data   : { *(.data .data.* .gnu.linkonce.d.*) *(.data.rel.ro .data.rel.ro.*) *(.got .got.plt) } :data
    .bss    : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gcc_except_table*) *(.fini_array*) *(.dtors) *(.gnu.version*) *(.gnu.hash) *(.interp) *(.dynsym) *(.dynstr) *(.dynamic) *(.rela.dyn) }
}

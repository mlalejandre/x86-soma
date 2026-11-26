/* SOMA_329_IA_LOCAL: como voz.ld (codigo R+X y datos R+W separados por pagina, nunca W+X) pero CONSERVA las tablas de excepciones
 * de C++ (.eh_frame, terminada en 0, y .gcc_except_table): llama.cpp avisa de los errores al cargar un modelo con excepciones. */
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
    .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :text
    .eh_frame : { PROVIDE(__eh_frame_start = .); KEEP(*(.eh_frame)) LONG(0); } :text
    . = ALIGN(4096);
    .init_array : { PROVIDE(__init_array_start = .); KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array .ctors)) PROVIDE(__init_array_end = .); } :data
    .data   : { *(.data .data.* .gnu.linkonce.d.*) *(.data.rel.ro .data.rel.ro.*) *(.got .got.plt) } :data
    .tdata  : { *(.tdata .tdata.*) } :data
    .tbss   : { *(.tbss .tbss.*) } :data
    .bss    : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame_hdr) *(.fini_array*) *(.dtors) *(.gnu.version*) *(.gnu.hash) *(.interp) *(.dynsym) *(.dynstr) *(.dynamic) *(.rela.dyn) }
}

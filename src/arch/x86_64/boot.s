.section .multiboot
.align 4
.long 0x1BADB002
.long 0x00000000
.long -(0x1BADB002 + 0x00000000)

/* SOMA_154_T480S: cabecera Multiboot 2 (GRUB la usa con la orden "multiboot2", tambien en UEFI). Pide un framebuffer
 * lineal de 32 bits sin imponer tamano (GRUB usa gfxpayload/gfxmode, o el modo nativo del firmware). La cabecera
 * Multiboot 1 de arriba sigue sirviendo para la entrada de QEMU de siempre ("multiboot"). */
.align 8
mb2_header_start:
.long 0xE85250D6
.long 0
.long mb2_header_end - mb2_header_start
.long -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))
.align 8
mb2_tag_fb:
.short 5
.short 1
.long 20
.long 0
.long 0
.long 32
.align 8
.short 0
.short 0
.long 8
mb2_header_end:

.section .text
.code32

.global _start
.extern kernel_main

_start:
    cli

    /* Preservar informacion de arranque entregada por GRUB/Multiboot */
    movl %eax, multiboot_magic
    movl %ebx, multiboot_info_addr

    /* SOMA_154_T480S: con Multiboot 2 la estructura de informacion puede estar en cualquier sitio por debajo de 4 GiB
     * (en UEFI, a menudo por encima del primer GiB, que es lo unico que se proyecta al entrar en modo largo). Aqui, en
     * modo protegido de 32 bits sin paginacion, se copia entera a memoria del nucleo. */
    cmpl $0x36D76289, %eax
    jne 1f
    movl (%ebx), %ecx
    cmpl $32768, %ecx
    jbe 2f
    movl $32768, %ecx
2:
    movl %ebx, %esi
    movl $mb2_copy, %edi
    cld
    rep movsb
    movl $mb2_copy, multiboot_info_addr
1:

    movl $stack_top, %esp

    call setup_long_mode

    lgdt gdt64_desc
    ljmp $0x08, $long_mode_start


setup_long_mode:

    movl $pml4_table, %edi
    xorl %eax, %eax
    movl $512, %ecx
    rep stosl

    movl $pdpt_table, %edi
    xorl %eax, %eax
    movl $512, %ecx
    rep stosl

    movl $pd_table, %edi
    xorl %eax, %eax
    movl $1024, %ecx
    rep stosl

    movl $pdpt_table, %eax
    orl $0x3, %eax
    movl %eax, pml4_table

    movl $pd_table, %eax
    orl $0x3, %eax
    movl %eax, pdpt_table

    movl $pd_table, %edi
    xorl %ebx, %ebx
    movl $512, %ecx

map_pd:
    movl %ebx, %eax
    orl $0x83, %eax
    movl %eax, (%edi)
    movl $0, 4(%edi)

    addl $0x200000, %ebx
    addl $8, %edi

    loop map_pd

    /*
     * Enable PAE and OSFXSR.
     *
     * PAE:
     *   CR4 bit 5
     *
     * OSFXSR:
     *   CR4 bit 9
     *
     * GCC may generate SSE/SSE2 instructions for x86-64
     * code. CR4.OSFXSR must therefore be enabled before
     * executing compiled C code that uses those instructions.
     */
    /* CR4: PAE (bit 5), OSFXSR (bit 9), OSXMMEXCPT (bit 10) = 0x620 */
    movl %cr4, %eax
    orl $0x620, %eax
    movl %eax, %cr4

    movl $pml4_table, %eax
    movl %eax, %cr3

    movl $0xC0000080, %ecx
    rdmsr
    orl $0x900, %eax    /* Bit 8 (LME) + Bit 11 (NXE: No-Execute Enable) */
    wrmsr

    /* CR0: limpiar EM (bit 2), fijar MP (bit 1), PG (bit 31), PE (bit 0) */
    movl %cr0, %eax
    andl $~(1 << 2), %eax
    orl $(1 << 1), %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    ret


.align 8
.global gdt64
.global gdt64_desc

gdt64:
    .quad 0x0000000000000000        /* 0x00: Descriptor Nulo */
    .quad 0x00AF9A000000FFFF        /* 0x08: Codigo Ring 0 64 bits */
    .quad 0x00AF92000000FFFF        /* 0x10: Datos Ring 0 64 bits */
    .quad 0x00CFF3000000FFFF        /* 0x18: Datos Ring 3 (SOMA_PARCHE_014_RING3; SYSRET: SS = STAR+8). Bit "accedido" ya puesto: la GDT esta en .text de solo lectura y la CPU escribiria ahi al cargar el selector */
    .quad 0x00AFFB000000FFFF        /* 0x20: Codigo Ring 3 64 bits (SYSRET: CS = STAR+16), accedido ya puesto */
    .quad 0x0000000000000000        /* 0x28: TSS low (cargado en C) */
    .quad 0x0000000000000000        /* 0x30: TSS high (cargado en C) */

gdt64_end:

gdt64_desc:
    .word gdt64_end - gdt64 - 1
    .long gdt64


.section .text
.code64

long_mode_start:

    /* Cargar selectores de segmento de datos de 64 bits canonicos */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    movq $stack_top, %rsp
    xorq %rbp, %rbp

    andq $-16, %rsp

    call ssp_randomize          /* PARCHE 132: canario aleatorio ANTES de que haya funciones protegidas en curso */
    call kernel_main

halt:
    hlt
    jmp halt

/* ====================================================================
 * Stubs de Interrupcion y Excepciones x86_64
 * ==================================================================== */
.macro ISR_NOERR index
.global isr_stub_\index
isr_stub_\index:
    pushq $0
    pushq $\index
    jmp isr_common_stub
.endm

.macro ISR_ERR index
.global isr_stub_\index
isr_stub_\index:
    pushq $\index
    jmp isr_common_stub
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31
ISR_NOERR 32
ISR_NOERR 33
/* PARCHE 108: IRQ 2..15 del PIC (vectores 34..47). Antes no tenian puerta: una IRQ espuria (7/15) acababa en #NP y panico del kernel */
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47
ISR_NOERR 64      /* PARCHE 038: interrupcion del temporizador del LAPIC (autotest de apic) */
ISR_NOERR 255     /* PARCHE 038: vector espurio del LAPIC (SVR = 0x1FF) */

.extern isr_exception_handler

isr_common_stub:
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax

    movq %rsp, %rdi
    cld
    call isr_exception_handler

    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    addq $16, %rsp
    iretq

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
    .quad isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
    .quad isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
    .quad isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
    .quad isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
    .quad isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
    .quad isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
    .quad isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
    .quad isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    .quad isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35
    .quad isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39
    .quad isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43
    .quad isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47

.section .text


.section .data
.align 8
.global multiboot_magic
.global multiboot_info_addr
multiboot_magic:
    .long 0
multiboot_info_addr:
    .long 0

.section .bss
.align 8
.global mb2_copy
mb2_copy:
    .space 32768
.section .bss

.align 16

stack_bottom:
    .skip 65536

stack_top:

.align 4096

pml4_table:
    .skip 4096

pdpt_table:
    .skip 4096

pd_table:
    .skip 8192

.section .note.GNU-stack,"",@progbits

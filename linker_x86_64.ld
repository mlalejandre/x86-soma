ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text ALIGN(4096) :
    {
        __text_start = .;
        . = ALIGN(4);
        KEEP(*(.multiboot))
        *(.text*)
        __text_end = .;
    }

    .rodata ALIGN(4096) :
    {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    }

    .data ALIGN(4096) :
    {
        __data_start = .;
        *(.data*)
        __data_end = .;
    }

    .bss ALIGN(4096) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_image_end = .;
    /* PARCHE 029: la virtqueue ya no esta clavada en 0x00800000. Va en la siguiente frontera de 2 MiB tras la imagen
       (hoy sigue siendo 0x00800000), asi que el kernel puede crecer hasta el limite real de la zona baja reservada por
       el PMM (16 MiB) en lugar de los ~57 KB de margen de antes. Al alinear a 2 MiB se conserva la pagina grande del
       arranque (sin partir ni cambiar permisos). */
    __virtq_start = ALIGN(0x200000);
    ASSERT(__kernel_image_end <= 0x01E00000,
           "ERROR: la imagen del kernel supera 30 MiB: no cabe la virtqueue dentro de la zona baja reservada (32 MiB, PMMC_LOWER)")

    .virtio_queue __virtq_start (NOLOAD) :
    ALIGN(4096)
    {
        KEEP(*(.virtio_queue))
    }
    __virtq_end = .;

    ASSERT(
        SIZEOF(.virtio_queue) <= 12288,
        "ERROR: virtqueue demasiado grande"
    )
    ASSERT(__virtq_end <= 0x02000000,
           "ERROR: la virtqueue sale de los 32 MiB reservados por el PMM (PMMC_LOWER)")

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}

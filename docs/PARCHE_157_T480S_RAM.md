# Parche 157 — T480s: usar toda la RAM

Segundo parche del plan (`docs/PLAN_T480S.md`). Requiere el 156C.

## El problema

El asignador de marcos físicos (PMM) tenía un mapa de bits fijo de 512 MiB. En el T480s, con 16 GiB, SOMA usaba el **3 %**
de la memoria. El montón del núcleo, donde viven las aplicaciones, el navegador y las imágenes, tenía además un límite de
256 MiB y una ventana virtual dentro del primer GiB, sin sitio para crecer.

## La solución

### Dos reservas en el PMM
El núcleo usa la dirección física como puntero en varios sitios: tablas de páginas, DMA de los drivers, pilas de procesos.
Eso solo funciona en la memoria proyectada en identidad (el primer GiB). Por eso hay dos reservas sobre un único mapa de bits:

| Reserva | Rango | Para qué | Función |
|---|---|---|---|
| Baja | 16–512 MiB | tablas de páginas, DMA, todo lo que usa la dirección física como puntero | `pmm_alloc_frame()`, `pmm_alloc_contig()` (sin cambios) |
| Alta | 512 MiB – techo de la RAM | memoria que solo se usa a través de una proyección: el montón | `pmm_alloc_frame_high()` (nueva) |

Ningún uso existente cambia: lo que pedía marcos sigue recibiéndolos de la reserva baja. Solo el montón pasa a la alta.
La auditoría confirmó que todo el DMA (xHCI, NVMe, AHCI, HDA, virtio) sale de `pmm_alloc_contig` o de búferes estáticos,
y que ningún puntero del montón se guarda en 32 bits.

El mapa de bits se dimensiona con la RAM real: en el T480s cubre hasta `0x4_6180_0000` (17,5 GiB de direcciones) y
ocupa 560 KiB, colocados en la primera zona libre de la reserva baja. Con 512 MiB o menos se usa el mapa estático de siempre.

### El montón se muda
De `0x2000_0000` a `0x40_0000_0000` (256 GiB virtuales): lejos de la identidad, de las tablas del firmware (1–4 GiB) y
del framebuffer. Sigue dentro de `PML4[0]`, que comparten todos los espacios de direcciones. Su límite pasa a ser **la
mitad de la RAM, hasta 8 GiB**: en el T480s, ~7,8 GiB, frente a los 256 MiB de antes.

### Por qué no se amplió el mapa de identidad (desviación del plan)
El plan proponía proyectar toda la RAM en identidad. Con las dos reservas no hace falta, y ampliar la identidad habría
chocado con la ventana antigua del montón y con `hw_map_phys`. Si un parche futuro necesita acceder por identidad a
memoria alta (p. ej. DMA de 64 bits a gran escala), se puede añadir entonces.

## Órdenes

- **`hw mem`:** ahora muestra lo libre en cada reserva y el límite del montón.
- **`ramtest [MiB]`** (nueva): reserva esa cantidad en el montón en bloques de 1 MiB y escribe un patrón que depende de la
  dirección. Después lo verifica y lo libera, e informa de las direcciones físicas usadas y de la velocidad. Por defecto,
  1024 MiB. Ojo: el montón no devuelve páginas al PMM, así que tras un `ramtest` grande esa memoria queda proyectada y
  libre dentro del montón (reutilizable por cualquier `kmalloc`).
- **Banner del terminal:** muestra la RAM real (antes un texto fijo de «512 MiB»).

## Interruptor
`soma.ram=512` (o `soma.ram=off`, o la entrada «modo seguro») limita el PMM a los primeros 512 MiB, como antes del parche.

## Pruebas realizadas
- **Núcleo del PMM en el anfitrión** (`tests/test_pmm_157.c`, ASan/UBSan), el mismo código que usa el núcleo, con cinco mapas:
  - **T480s reconstruido con tus datos:** se agotan los 15,7 GiB marco a marco. Sin duplicados, ninguno en huecos, en la zona reservada ni en el propio mapa, y el DMA siempre en la reserva baja.
  - **QEMU con 512 MiB:** mapa estático, el montón cae a la reserva baja como antes.
  - **QEMU con 4 GiB.**
  - **Un mapa sin sitio para el mapa de bits:** recorte seguro a 512 MiB.
  - **Un mapa corrupto:** el mapa no pasa de 64 GiB.
- **Pruebas del montón en el anfitrión** (`tests/test_heap.c`): 102 comprobaciones, 0 fallos.
- **QEMU q35 + UEFI con 6 GiB** (2 GiB bajo 4 GiB y 4 GiB encima, como el portátil):
  - SOMA gestiona 5,9 GiB;
  - `ramtest 2500` se verifica bien, con 969 MiB por encima de 4 GiB;
  - los autotests del montón y del PMM pasan.
- **Navegador con el montón por encima de 4 GiB:**
  - una página con 20 000 objetos JavaScript da el resultado exacto;
  - una página con 12 imágenes carga sin fallos.
- **Terminal de la interfaz:** `ramtest 1500` y `hw mem` funcionan.
- **Regresiones:** QEMU clásico con 256 MiB y 1 GiB, q35 con 512 MiB, y aarch64 compila sin avisos.

## En el portátil
`hw mem` debería decir «SOMA gestiona 15,7 GiB». Prueba `ramtest 4096`: la verificación debe salir correcta y con varios
GiB por encima de 4 GiB.

## Revertir
`python3 parche_157_t480s_ram.py --undo`

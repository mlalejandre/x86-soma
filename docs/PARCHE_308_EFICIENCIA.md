# Parche 308 · Eficiencia: memcpy y compañía rápidas en aarch64, y menos que compilar en x86_64

**Hace falta `make`.**

## memcpy, memmove, memset y memcmp en aarch64 (Raspberry Pi 5)
En x86_64 el parche 199 ya las había afinado (`src/core/memfn.h`). En aarch64 seguían **byte a byte**: copiar una fila de
pantalla de 1920 píxeles eran 7680 vueltas de bucle, y todo el núcleo las usa (pantalla, navegador, red, discos, zlib...).

`src/core/memfn_gen.h` (nuevo) hace lo mismo de 8 en 8 bytes, y de 32 en 32 en el bucle principal:
* **Solo accesos alineados**: el núcleo de aarch64 se compila con `-mstrict-align`, porque antes de activar la MMU, y en memoria
  de dispositivo o no cacheable, un acceso desalineado da un fallo.
* Si origen y destino no tienen la misma alineación (una cabecera Ethernet de 14 bytes, por ejemplo), lee palabras alineadas
  del origen y las junta con desplazamientos. Esas lecturas pueden tocar hasta 7 bytes de fuera del rango, pero siempre dentro
  de una palabra alineada que contiene algún byte del rango: la misma página, así que nunca falla.
* `memmove` hacia atrás va por palabras cuando las alineaciones coinciden.
* gcc no puede convertir los bucles en llamadas a `memcpy` (sería llamarse a sí mismas): comprobado en el código generado.

Medido con el mismo código para aarch64, en emulación (`qemu-aarch64`; en la Pi la proporción será menor):

| Operación | Antes | Ahora |
|---|---|---|
| `memcpy` de una fila de pantalla (7680 B) | 13,1 µs | 0,9 µs |
| `memcpy` de un paquete de red desalineado (1514 B) | 2,6 µs | 0,4 µs |
| `memset` de 64 KiB | 96,5 µs | 4,2 µs |

`tests/test_memfn_gen_308.c` compara las cuatro con la libc: todos los tamaños de 0 a 300 y varios grandes, todas las
alineaciones de 0 a 8 en origen y destino, todos los solapes de `memmove` y zonas de guarda. Son 416 571 comprobaciones. Pasa
con gcc 14 (ASan/UBSan), con clang y compilada para aarch64 con `-mstrict-align`. Es C portable, así que en tu Mac (ARM)
también se ejecuta.

`memfn.h` (x86_64) y `memfn_gen.h` (aarch64) pasan a estar protegidos frente a los parches de la IA, igual que `mem.c`, que ya
lo estaba (`protected.txt` y `agent_protegidos.h`; la auditoría comprueba que las dos listas coinciden: 80 = 80).

## Menos que compilar en x86_64
Los 9 módulos de protocolos de la Pi 5 (`sd_proto`, `pcie_proto`, `mbox_proto`, `rp1_proto`, `gem_proto`, `dma_pool`,
`gpio_proto`, `sdio_proto` y `brcm_proto`) se compilaban también en x86_64, y el enlazador los tiraba enteros: ninguno dejaba
un solo símbolo vivo. Ahora solo se compilan en aarch64. Sus pruebas del anfitrión siguen igual.

## Lo que se miró y no se hizo
* Pasar `ce_inst1` (538 KB) y `np_inst1` (159 KB) de `.data` a `.bss`: harían la imagen 700 KB más pequeña, pero el límite de
  46 MiB cuenta también el `.bss`, así que no se gana margen, y habría que garantizar que se inicializan antes de su primer uso
  en decenas de sitios. No compensa.
* Activar `--gc-sections` en aarch64: necesita probarse en una Pi 5 real (`KEEP` de las secciones de arranque).

## Comprobado
`make ci` (gcc 14), x86_64 y aarch64 con `WERROR=1` desde cero, `boot-smoke` en las dos, y `--revertir` deja el árbol idéntico.

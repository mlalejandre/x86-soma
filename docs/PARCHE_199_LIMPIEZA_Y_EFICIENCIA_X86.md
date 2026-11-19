# Parche 199: limpieza y eficiencia en x86_64

Requiere el parche 198. Despues de aplicarlo hay que compilar desde cero (`make clean`). No se pierde ninguna funcion.

## 1. memcpy, memmove, memset y memcmp nuevos (`src/core/memfn.h`)

El nucleo copiaba siempre con `rep movsb`, y hacia atras con `std; rep movsb`, que ninguna CPU acelera. Los servicios de ring 3
(decodificadores de imagenes, zlib, PDF, red) copiaban con un bucle de un byte por vuelta. Ahora, en ambos:

- hasta 32 bytes, sin bucle ni `rep` (cargas y escrituras de 8/4/2 bytes solapadas);
- mas de 32, `rep movsq` / `rep stosq` (8 bytes por paso);
- memmove hacia atras de 8 en 8; memcmp de 8 en 8.

Medido con la orden nueva `membench` en QEMU sin virtualizacion (ciclos por llamada, antes -> despues):

| bytes | memcpy | memset | memmove atras |
|---|---|---|---|
| 8 | 68 -> 41 | 55 -> 37 | 76 -> 37 |
| 64 | 279 -> 66 | 206 -> 55 | 280 -> 75 |
| 1024 | 3875 -> 525 | 2833 -> 385 | 3991 -> 535 |
| 65536 | 262638 -> 33172 | 190591 -> 22495 | 261440 -> 33561 |

En un emulador la mejora es de 1,5 a 8 veces. En una CPU real con ERMS (el T480s) la ganancia esta en las copias pequenas, que
son la mayoria, y en memmove hacia atras; en bloques grandes el rendimiento es equivalente. Ejecuta `membench` antes y despues
en el portatil para ver la cifra real. Prueba: `tests/test_memfn_199.c` (245.503 comprobaciones contra la libc: todos los tamanos
de 0 a 300, alineaciones y solapes).

## 2. El servidor web ya no despierta la CPU mil veces por segundo

El hilo `httpd` esperaba conexiones dentro de un bucle que no duerme: se despertaba en cada tic del reloj aunque nadie se
conectase. En reposo era, con diferencia, el hilo que mas veces se planificaba. Ahora mira 50 veces por segundo y duerme entre
medias (unas 5 veces menos activaciones; las peticiones se siguen atendiendo en 5-25 ms).

## 3. Codigo inalcanzable fuera del binario (x86_64)

Cada funcion va a su seccion y el enlazador descarta lo que nadie referencia: unas 680 secciones, 62 KiB de codigo. La mayor parte
son los modulos de la Raspberry Pi (`brcm_proto`, `sdio_proto`, `gem_proto`, `gpio_proto`...), que se enlazaban tambien en el
nucleo de PC, y partes de uACPI que SOMA no usa. `make GC=0` lo desactiva. La lista completa de lo descartado esta en
`docs/CODIGO_SIN_USO_199.txt`: lo que aparece ahi fuera de uACPI y de los modulos de la Pi son funciones que nadie llama en x86
y se pueden borrar del codigo fuente cuando quieras.

## 4. Limpieza

- `conctest`: recogia hilos muertos durante la prueba y contaba como fuga cualquier cambio en el numero de bloques. Ahora recoge
  antes de contar, espera a que se asiente y solo considera fuga que queden MAS bloques; si falla dice cuantos. (Queda un caso
  conocido: la primera ejecucion tras usar la red puede contar 1 bloque de mas, una reserva unica de otro hilo; a la segunda pasa.)
- Comentario obsoleto de `ssp.c` corregido (el canario ya es aleatorio desde hace varios parches).

## Como se ha medido

Perfil por muestreo del contador de programa (500 muestras con el escritorio en reposo): 59 % `cpu_idle`, ~28 % el dibujo del
orbe (cintas y halo), 2 % el bucle de `httpd`, el resto repartido. El orbe ya esta muy trabajado (punto fijo, mosaicos, varios
nucleos) y no se ha tocado.

## Archivos

Nuevos: `src/core/memfn.h`, `tests/test_memfn_199.c`, `docs/CODIGO_SIN_USO_199.txt`. Cambiados: `src/core/mem.c`,
`src/user/sbxrt.h`, `src/net/httpd.c`, `src/core/conc_test.c`, `src/core/ssp.c`, `src/agent/shell.c` (`membench`), `Makefile`.

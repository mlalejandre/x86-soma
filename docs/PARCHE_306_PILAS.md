# Parche 306 · Pilas del núcleo: un desbordamiento siempre se ve

**Hace falta `make`** (cambian las opciones de compilación del núcleo y varios archivos de C).

## El problema
Cada hilo del núcleo tiene 60 KiB de pila y, debajo, **una** página de guarda de 4 KiB sin mapear. Si la pila se agota y se
toca la guarda, salta un fallo de página: el sistema se para con su traza. Pero eso solo funciona si la pila baja de 4 KiB en
4 KiB. Hay más de 50 funciones del núcleo, sin contar el navegador, con marcos mayores (la mayor, `mc_par_redundante`, usa
9,4 KiB): bajan el puntero de pila de golpe. Si la guarda queda en medio del marco, nadie la toca, y la función escribe en la
pila del hilo vecino **sin que nada falle**. Es el tipo de fallo que aparece semanas después y en otro sitio.

Las pilas grandes del navegador y de `wn_fetch` (256 KiB a 1 MiB, pedidas con `kmalloc`) ni siquiera tienen guarda: si se
desbordan, pisan el bloque del montón que tengan debajo.

## Qué cambia
* **`-fstack-clash-protection`** (`Makefile`, x86_64): el compilador toca cada página al reservar un marco grande, así que un
  desbordamiento cae siempre en la guarda. Son 94 sitios en el núcleo; el código crece 576 bytes. Se quita con `make CLASH=0`.
  En aarch64 sigue desactivado, como el protector de pila, hasta poder probarlo en la Pi.
* **Límite de marco** (`-Wframe-larger-than=16384`): con `WERROR=1`, como en la CI, ninguna función nueva del núcleo puede reservar
  más de 16 KiB de pila de golpe (hoy las mayores son de 9,4 KiB en x86_64 y de 14 KiB en aarch64, en `jpeg.c`). El motor web
  (`web_net.c`, `web_page.c`, `web_layout.c`, `web_js_dom.c`) se salta el límite: corre en sus pilas propias y tiene marcos de
  hasta 84 KiB. Se cambia con `make KFRAME_MAX=...`.
* **El informe de pánico dice qué hilo se ha desbordado** (`idt.c`, `thread.c`): si la dirección del fallo es la página de guarda
  de un hilo, añade `DESBORDAMIENTO DE PILA del hilo '<nombre>'`. Funciona porque `#PF` y `#DF` usan su propia pila (IST1), y
  así el informe se puede escribir aunque la pila del hilo esté agotada.
* **Canario en las pilas propias del navegador y de `wn_fetch`** (`src/core/pila_canario.h`): los primeros 256 bytes llevan un
  patrón que se comprueba al volver de cada uso (pila de la GUI y de `wn_fetch`) y en cada vuelta del hilo trabajador. Si se ha
  roto, se dice qué pila era y se detiene (`[PILA] desbordada...`). Así el sistema no sigue con el montón corrupto.
* `usermode.c`: `user_range` leía `r->npages` y **después** miraba si `r` era nulo.

## Pruebas nuevas
* `crash pila`, una orden de depuración: un hilo agota su pila con marcos de 8 KiB. `fault_check.py` tiene el escenario
  «pila desbordada»: el sistema tiene que pararse con pánico observable (código 71), la traza tiene que llegar a
  `pila_prueba_baja` y el informe tiene que nombrar el hilo `pila_prueba`. Ojo: con `CLASH=0` también pasa, porque en esa
  disposición una escritura cae justo en la guarda. La prueba comprueba la detección y el informe; que el compilador sondee
  cada página se vio en el código generado.
* `tests/test_pila_canario_306.c`: el canario detecta cualquier byte cambiado y no se confunde con el pintado de las pilas.
* `tests/test_sched.c`: `thread_guard_owner` atribuye solo la página de guarda al hilo y deja de hacerlo al liberarlo.

## Comprobado
`make ci` (gcc 14), `make ARCH=x86_64 WERROR=1` y `make ARCH=aarch64 WERROR=1` con gcc 14 desde cero, `boot-smoke` en las dos,
y en x86_64 `sbx-check`, `idle-check`, `cpu-matrix` y `fault-check` (21 escenarios). Después, `--revertir` deja el árbol idéntico.

# Parche 328 · Compilar mucho más rápido

**Hace falta `make`.** La primera vez después del parche se recompila todo una vez, porque cambian las opciones. A partir de ahí
solo se recompila lo que cambia.

## El problema
Cada compilación tardaba lo mismo que una desde cero, y en un solo procesador:

1. **`ejecutar.py` borraba `build/` en cada ciclo** y llamaba a `make` **sin `-j`**. `construyeimagen.py` y la opción del T480s
   también borraban `build/` entera.
2. **Cada objeto del núcleo dependía de todas las cabeceras** (`$(HEADERS)`): tocar un `.h` cualquiera recompilaba el núcleo
   entero.
3. **Los programas de ring 3** (navegador aislado, Python, reproductor, voz, JavaScript, servicios aislados) se compilaban con
   bucles `for` en serie, y siempre todos sus archivos, aunque solo cambiara uno.
4. Un fallo que lo anterior tapaba: la regla de `websvc.elf` usaba `$(HEADERS)` antes de definirla, así que le llegaba vacía y
   **el navegador aislado no se recompilaba al cambiar una cabecera**. Sin el borrado de `build/` habría quedado desfasado.

## Qué cambia
* **`make` usa todos los procesadores** si no se le da `-j` (para `all`, `t480s` y `t480s-usb`). Las pruebas (`test`, `ci`,
  `fuzz`…) siguen en serie: algunas abren puertos y no deben correr a la vez. `make -j1` lo desactiva.
* **Dependencias de verdad en el núcleo**: `gcc -MMD -MP` escribe junto a cada objeto las cabeceras que incluye (`.d`). Además,
  `build/<arq>/.cflags` guarda el compilador y las opciones y solo cambia si cambian (`EXTRA_CFLAGS`, `PLATFORM`, `WERROR`…).
  Así, cambiar de opciones recompila lo necesario sin borrar nada.
* **`tools/lote_cc.py`** (nuevo) sustituye a los bucles en serie. Compila en paralelo, compartiendo los trabajos de `make -jN`
  (su servidor de trabajos, para no pasarse de procesos), y solo lo que hace falta. Un objeto se recompila si:
  * no existe;
  * cambió la orden con la que se hizo;
  * la fuente es más nueva que él;
  * o lo es alguna de las cabeceras que incluyó.

  También borra los objetos de fuentes que ya no existen.
* `ejecutar.py` y `construyeimagen.py` **ya no borran `build/`**. El pendrive del T480s se compila en `build/t480s` (otras
  opciones) y no pisa la compilación normal. `SOMA_LIMPIO=1 python3 ejecutar.py` fuerza empezar de cero.

## Medidas (equipo de prueba con 2 procesadores)
| Caso | Antes | Ahora |
|---|---|---|
| Compilación completa | 301 s (en serie) | 158 s |
| `make` sin cambios | (con `ejecutar.py`: compilación completa) | 0,2 s |
| Tocar `src/agent/llm.c` | (completa) | 1 s |
| Tocar `src/apps/web_net.h` | núcleo entero + navegador aislado | 16 s (10 objetos del núcleo y 7 de ring 3) |
| Tocar `src/core/mem.h` (la incluye casi todo) | núcleo entero | 71 s |

Con más procesadores la mejora de la compilación completa crece casi en proporción. En un Mac con 8 o 10 núcleos debería bajar a
una fracción de lo que tardaba.

## Notas
* `make -n` ejecuta igualmente los lotes (llevan `+`, para recibir los trabajos de `make`): compila, no solo enseña.
* Si Docker en un Mac con Apple Silicon emula x86 sin Rosetta, todo va varias veces más lento. En Docker Desktop:
  *Settings → General → «Use Rosetta for x86_64/amd64 emulation on Apple Silicon»*.

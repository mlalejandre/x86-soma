# Parche 324 · La prueba de `cc` en QEMU vuelve a pasar (16/16)

**No hace falta `make`**: solo cambia una prueba (`tests/prueba_cc_265.py`).

## El problema
`python3 tests/prueba_cc_265.py` daba **7/16** desde antes del parche 305 (comprobado con el árbol del 313 sin cambios). El
compilador funcionaba; fallaba el arnés que escribe los programas en C dentro de SOMA a través de las órdenes de `fw_cfg`:

1. Para que la orden no llevara `%`, la prueba lo cambiaba por `$` y un script de Python lo deshacía. Pero la consola de SOMA
   **expande variables** (`$d`, `$s`...) antes de ejecutar la orden: `printf("%d")` llegaba como `printf("")` y todo lo que dependía
   de un formato fallaba («hola desde C:» sin números, «numeros .3f .2e»).
2. La orden que escribía `win.c` medía 1054 caracteres y el lector de órdenes corta las líneas en 1 KiB: el script quedaba roto y
   `win.c` no existía, así que fallaban las tres pruebas de ventanas.

## Qué cambia
El programa en C se manda **en hexadecimal** y por **trozos de 400 bytes** (el primero crea el archivo, los demás añaden): la orden
no lleva `;`, `%`, `$` ni barras, y la más larga mide 934 caracteres.

## Pruebas
* `python3 tests/prueba_cc_265.py`: **16/16** (antes 7/16).

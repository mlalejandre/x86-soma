# Parche 264 · SOMA-LANG sin límites pequeños

Requiere el 263. Se aplica desde la raíz de SOMA:

```
python3 parche_264_somalang_grande.py
python3 parche_264_somalang_grande.py --comprobar
```

`--comprobar` pasa `make pruebas-lang`. Para quitarlo: `python3 parche_264_somalang_grande.py --revertir`.

## Qué cambia

El formato del bytecode ya admitía programas enormes: los saltos son de 32 bits. Lo que limitaba eran tamaños fijos.

| | Antes | Ahora |
|---|---|---|
| Elementos por array | 48 | crece según haga falta (1 000 000 como máximo; en la práctica manda el presupuesto) |
| Arrays vivos a la vez | 48 | 8192 |
| Entradas por tabla | 48 | 262 144, con índice hash a partir de 32 |
| Tablas vivas | 48 | 8192 |
| Cadenas largas vivas (más de 127 caracteres) | 24 | 1024 |
| Caracteres por cadena | 16 384 | 1 048 576 |
| Memoria para arrays, tablas y cadenas | fija: unos 1,1 MB por programa | la que haga falta, hasta **64 MB por programa** |
| Pila de la VM | 256 | 1024 |
| Variables globales | 128 | 1024 |
| Locales (entre todas las llamadas en curso) | 384 | 2048 |
| Llamadas anidadas (recursión) | 128 | 512 |
| Funciones | 64 | 512 |
| `break` / `continue` | 32 | 256 |
| Programa con `run` | 16 KB de fuente, 64 KB de bytecode | 256 KB y 512 KB |
| Programa del programador / Studio | 32 KB | 64 KB |

- **Presupuesto.** Cada programa tiene 64 MB para sus arrays, tablas y cadenas. Si se pasa, la operación no se hace, como antes al llenar un array de 48. Se avisa una vez («[VM] Sin sitio para … (memoria del programa: N de 64 MB)») y el programa sigue. Nunca se le quita memoria al núcleo.
- **El valor más grande que cabe.** Un valor de SOMA-LANG ocupa 152 bytes (lleva dentro una cadena de 127 caracteres) y, al crecer, un array necesita a la vez su copia vieja y la nueva. Por eso un solo array de enteros llega a unos **260 000 elementos**. Varios arrays juntos pueden llenar los 64 MB.
- **Recolector.** Ahora libera de verdad la memoria de lo que ya no se usa. Si falta sitio, recoge una vez y, si sigue sin haber, no lo repite en cada operación.
- **Tablas.** A partir de 32 entradas buscan con un índice hash. Las igualdades son las de siempre: `nil` y `0` son la misma clave, y los arrays y las tablas se comparan por identidad. Las tablas pequeñas siguen con la búsqueda lineal de antes.
- **Memoria fija de la VM.** Pasa de 458 KB a 627 KB (pila, variables y locales más grandes). Las tablas ya no reservan 700 KB nada más crear la primera, ni las cadenas 384 KB.
- **Arreglo de paso.** `vm_init` leía `table_pool` de memoria sin inicializar (las VM se piden con `kmalloc`). Ahora deja todo a cero, y la autoprueba de la VM libera su memoria.

## Medido

- **`tests/test_lang.c` (76 comprobaciones) y las tres del programador** (`test_coder_sim` 49, `test_coder_235` 27, `test_coder_py_239` 33): igual que antes, 0 fallos.
- **`tests/test_lang_264.c` (nueva, 53 comprobaciones; con ASan y LeakSanitizer, sin fugas).** Cubre:
  - Arrays de 100 000 elementos y 3000 arrays anidados.
  - 20 000 arrays desechados (más que el máximo vivo).
  - 150 MB de basura en total con un presupuesto de 64 MB.
  - Llenar el presupuesto: se avisa una vez y el programa sigue.
  - Tablas de 20 000 claves de texto y 5000 numéricas; `nil` y `0` como la misma clave; claves largas.
  - Cadenas de 200 000 caracteres y 300 cadenas largas vivas.
  - 900 variables, 300 funciones y una recursión de 400.
  - El error claro con 1100 variables y un programa de 4000 líneas (más de 64 KB).
- **Compilación:** gcc y clang con `-Wall -Wextra -Werror`, y `vm.c` como en el núcleo (`-ffreestanding -mgeneral-regs-only`).
- **Velocidad (en el ordenador de pruebas, sin ASan):** 100 000 `push` en 0,02 s; 20 000 inserciones en una tabla en 0,01 s.
- **No probado:** el núcleo entero en QEMU ni el Studio con un programa real de 64 KB.

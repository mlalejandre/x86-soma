# Parche 314 · Los programas grandes de ring 3 van comprimidos en la imagen

Primer paso para adelgazar la imagen del núcleo. **Hace falta `make`.**

## El problema
Seis programas de ring 3 van incrustados en la imagen tal cual: el navegador (`websvc`), el reproductor (`vplay`), `voz`,
`js`, `cc` y `python`. Suman **8,2 MB** de los 46 MiB de presupuesto (la imagen estaba al 68,8 %). Son ELF con mucho relleno
y tablas: comprimen a la mitad.

## Qué cambia
* **`tools/progz.py`** (nuevo): al compilar, cada `build/x86_64/user/X.elf` se convierte en `X.elf.z`, que es lo que se
  incrusta: una cabecera de 24 bytes (`SOMZ`, versión, tamaño original y huella FNV-1a 64 del original) y el ELF en zlib.
  `progz.py --prueba` hace una ida y vuelta (lo usa `make test`).
* **`src/arch/x86_64/progz.c`** (nuevo): `progz_abre` comprueba la cabecera, descomprime con **el servicio aislado `zdec`**
  (ring 3, parche 139: el inflate no corre en el núcleo) y comprueba el tamaño y la huella; `progz_cierra` libera. El cargador
  de ELF copia los segmentos a sus marcos, así que la copia descomprimida se libera en cuanto el proceso está creado.
* **`src/core/progz_fmt.h`** (nuevo): el formato, en funciones puras que se prueban en el anfitrión.
* `shell_procesos.c` (`python`, `cc`), `shell_js.c` (`js`), `voz_sys.c`, `vplay_sys.c` y `web_r3.c` (el navegador)
  descomprimen, lanzan y liberan. El navegador descomprime **una vez** antes de su bucle de reintentos.
* Si algo no cuadra (cabecera, tamaño, huella, o `zdec` falla) el programa **no se ejecuta** y la consola lo dice
  (`[progz] nombre: ...`). No hay vuelta atrás a una copia sin comprimir: no la hay en la imagen.
* `progz.c`, `progz.h` y `progz_fmt.h` quedan protegidos frente a la IA (89 = 89).
* Solo x86_64: en aarch64 estos programas no existen (no hay EL0).

## Resultado
| programa | antes | dentro de la imagen |
|---|---:|---:|
| vplay | 2 488 KB | 1 308 KB |
| websvc (navegador) | 2 262 KB | 1 198 KB |
| voz | 1 636 KB | 691 KB |
| jsrun | 854 KB | 387 KB |
| cc | 592 KB | 242 KB |
| python | 391 KB | 179 KB |
| **total** | **8 222 KB** | **4 006 KB** |

La imagen pasa del **68,8 % al 60,3 %** del presupuesto (27,8 de 46 MiB): quedan 18,2 MiB libres.

El precio es descomprimir al lanzar: en QEMU sin aceleración, los seis juntos tardan 0,7 s; en hardware real, bastante menos.
La copia descomprimida vuelve al montón del núcleo justo después de crear el proceso.

## Pruebas
* **Anfitrión** (`tests/test_progz_314.c`): huellas con vectores conocidos (las mismas que `progz.py --prueba`), lectura de la
  cabecera y rechazo de firma, versión, tamaño 0, tamaño excesivo y cabecera sin datos.
* **QEMU** (`make sbx-check`): se descomprimen los seis programas y todos son ELF de 64 bits; una copia con un byte cambiado en
  el cuerpo, en la huella o en la firma, o vacía, **no se ejecuta**; sin pérdidas de memoria. El navegador real sigue cargando
  páginas (`--web`).
* **QEMU** (`tests/prueba_python_237.py`, `prueba_python_239.py`: 15/15 y 8/8) y **`tests/prueba_progz_314.py`** (nueva): python,
  js y cc arrancan dos veces seguidas y no aparece ningún aviso `[progz]`.

`tests/prueba_cc_265.py` da 7/16 **igual que sin este parche**: en el arnés se pierde lo que va detrás de `%` en las órdenes y
`win.c` no llega a escribirse. No es de este parche; queda anotado.

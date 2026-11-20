# MicroPython en SOMA

Copia de **MicroPython 1.30.0-preview** (https://github.com/micropython/micropython, commit
`056fe733dea5441c36e43f4806c4133c350b5f94`, licencia MIT: ver `LICENSE`). La usa `src/user/python.c`, el intérprete que lanza
la orden `python` como proceso de ring 3 (ver la regla `$(BUILD)/user/python.elf` del Makefile).

## Qué hay aquí

| Carpeta | Origen |
|---|---|
| `py/`, `genhdr/`, `extmod/modplatform.h`, `shared/runtime/gchelper*`, `port/` | el paquete que genera `ports/embed/embed.mk` |
| `extmod/mod{random,json,re,heapq,binascii}.c`, `lib/re1.5/` | `extmod/` y `lib/re1.5/` de MicroPython (re1.5 se incluye desde `modre.c`) |
| `libm_dbl/` | `lib/libm_dbl/` de MicroPython (musl); `libm_dbl/include/math.h` es nuestro: evita el `math.h` de glibc |
| `libc/string0.c` | `shared/libc/string0.c` (memcpy, memset, strlen...) |

La configuración es `src/user/python/mpconfigport.h`.

## Si cambia la configuración o `src/user/python.c`

`genhdr/` (qstr, módulos, punteros raíz) se genera a partir de la configuración y de los archivos que se escanean. Si se añade
un nombre nuevo (`MP_QSTR_...`), un módulo (`MP_REGISTER_MODULE`) o un puntero raíz, o cambia `mpconfigport.h`, hay que
regenerarlo con un clon de MicroPython en el mismo commit:

```
mkdir gen && cd gen
cp ../src/user/python/mpconfigport.h .
cp ../src/user/python.c python_scan.c
cp ../third_party/micropython/Makefile.upylib.soma Makefile.upylib    # ajusta MICROPYTHON_TOP y la ruta de src/user
make -f Makefile.upylib
cp micropython_embed/genhdr/* ../third_party/micropython/genhdr/
```

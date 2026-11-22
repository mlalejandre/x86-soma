# Parche 265 · Compilador de C en SOMA (TinyCC) y el programador en C

SOMA trae un **compilador de C**. Es TinyCC (`third_party/tinycc`, licencia LGPL 2.1) acompañado de una biblioteca de C
propia (`src/user/cc`). Funciona como Python: va dentro del núcleo y se ejecuta como un **proceso de ring 3**. Un error del
compilador o de un programa compilado nunca tumba el sistema. Los programas que genera son ejecutables normales de SOMA:
ELF estático, W^X, como mucho 4 segmentos y 12 MiB. Solo en x86_64.

```
cc hola.c                      # compila /workspace/hola.c -> /workspace/hola.elf
cc -run hola.c uno dos         # compila y ejecuta (argv = programa, uno, dos)
cc -run hola.elf tres          # ejecuta uno ya compilado
cc a.c b.c -o juego.elf        # varios archivos;  -c solo compila (.o);  -w sin avisos;  -Dnombre=valor;  -Iruta
cc -heap 4 grande.c            # MiB de memoria dinamica del programa (8 si no se dice)
```

En el **Terminal** del escritorio la salida aparece al momento, lo que escribes llega a `scanf`/`fgets` y **Esc** para el
programa, igual que con Python. Cuando un programa termina con un código distinto de 0, se indica. Si termina por un fallo
(puntero nulo, división por cero, pila agotada), también se indica y el sistema sigue funcionando.

## Ventanas con `soma.h`

Es la misma ventana que usan `win.*` en SOMA-LANG y `soma.win` en Python:

```c
#include <stdio.h>
#include <soma.h>
int main(void)
{
    if (!soma_win_create("Hola", 320, 200)) return 1;     /* 0 = no hay escritorio */
    int n = 0; char t[32];
    while (soma_win_active()) {                            /* 0 al cerrarla (tambien con Esc) */
        struct soma_mouse m = soma_win_mouse();            /* x, y, clicked, button, inside */
        if (m.clicked) n++;
        if (soma_win_key() == SOMA_KEY_UP) n += 10;        /* SOMA_KEY_UP/DOWN/LEFT/RIGHT, 10 Intro, 32 espacio, 'a'... */
        soma_win_clear(0x101828);
        snprintf(t, sizeof t, "Clics: %d", n);
        soma_win_text(20, 30, t, 0xFFFFFF);
        soma_win_rect(20, 80, 120, 40, 0x2563EB);
        soma_win_circle(220, 100, 30, 0xDC2626);
        soma_win_present();
        soma_sleep(16);
    }
    return 0;
}
```

También están `soma_win_line`, `soma_win_pixel`, `soma_win_size` y `soma_win_close`, más `soma_ms()` (milisegundos desde el
arranque), `soma_sleep(ms)` y `soma_clock()` (fecha y hora del equipo).

## Qué incluye la biblioteca de C

Escrita para SOMA, sin glibc. La misma biblioteca sirve para construir el propio compilador y para los programas que compila.
Estos la llevan compilada en su interior, así que no hay archivos de biblioteca que instalar.

- **stdio**: `printf` y toda su familia, `scanf`, `sscanf`, `fscanf`, `fopen`/`fread`/`fwrite`/`fseek`/`ftell`/`fgets`, `perror`...
  Los archivos son los del VFS. Las rutas relativas parten de `/workspace` y **solo se escribe en `/workspace`**. Las rutas
  privadas (`/etc`, `/agent`, `/mem`...) no se pueden leer.
- **Coma flotante exacta, como glibc**: `printf` con `%f %e %g %a` convierte el valor binario a decimal sin perder precisión,
  usando enteros grandes y redondeo al par. `strtod`, `strtof` y `strtold` redondean siempre bien. Con valores al azar, la
  salida es idéntica a la de glibc: 20 000 números en `printf` (cinco formatos cada uno), 200 000 cadenas en `strtod`/`strtof`
  y 20 000 en `strtold` (long double).
- **stdlib/string/ctype/math/time**: `malloc` con listas libres, unión de bloques vecinos y `realloc` que crece en el sitio;
  `qsort`, `bsearch`, `strto*`, `rand`; `string.h` y `strings.h` completos; `math.h` con la x87 (`sqrt`, `sin`, `cos`, `tan`,
  `atan2`, `exp`, `log`, `pow`, `fmod`, `floor`...); `time`, `localtime` (hora del equipo), `strftime`, `clock`; `setjmp` y
  `longjmp`; `atexit`; `assert`; `errno` con mensajes en castellano; `wchar.h` y UTF-8 (`mbstowcs`...); atómicos de C11.
- **Memoria de un programa**: 12 MiB en total. Incluyen el código, los datos, 8 MiB de memoria dinámica (cambia con `-heap`) y
  1 MiB de pila (la de SOMA es de 64 KiB: la biblioteca pone una suya). Si un programa no cabe, el compilador lo dice y deja el
  ejecutable vacío para que no se pueda cargar. No hay `fork`, hilos, red ni `mmap`.

## El programador y SOMA Studio, en C

- **SOMA Studio → Crear con IA**: junto a *Lenguaje* está el botón **C**. También se elige C escribiendo «en C» o «en lenguaje
  C» en la descripción, o pidiendo un archivo `.c`. «En casa», «C++» y «C#» no se toman por C.
- El modelo recibe la referencia de C de SOMA y un ejemplo verificado. Los dos ejemplos (de consola y con ventana) se compilan
  y ejecutan con el compilador de verdad en las pruebas. El código va en un bloque ```` ```c ````.
- El programador comprueba el código compilándolo de verdad (`cc -s`, sin escribir nada). Un error llega a la reparación con
  su línea, el texto de TinyCC y el código de alrededor numerado.
- En los programas de consola ejecuta las pruebas del encargo con `cc -x` (compilar y ejecutar con la entrada de cada caso).
  Detecta los fallos de lógica y los programas que se caen, y los repara con su pista correspondiente.
- Se rechazan con una pista `conio.h`, `windows.h`, SDL, `getch()`, `system()`, una ventana sin `soma_win_present` o sin
  bucle, una ventana que lee la consola, las flechas sin `SOMA_KEY_*` y la hora sacada de `soma_ms()`.
- **Normalizador**: añade los `#include` que falten para lo que usa el programa. Los modelos los olvidan a menudo y TinyCC
  solo avisa, aunque una llamada sin prototipo pasa mal los `double`. También corrige las comillas tipográficas y los `\r`.
- Las lecciones van a su propio archivo, `/agent/coder_lessons_c.txt`. *Probar* lanza el programa en el Terminal con
  `cc -run`. *Verificar* del editor compila con el compilador de verdad. La barra de estado muestra «C» y los archivos `.c`
  salen en violeta.

## Un fallo de TinyCC corregido

`(long double)y * f(x)`, cuando había que convertir `x` a `long double`, daba un resultado equivocado. TinyCC llamaba a
`save_reg(RC_ST0)` pasando una clase de registro donde va un registro (`x86_64-gen.c`), así que no guardaba `st0`. Ahora es
`save_reg(TREG_ST0)`. La batería oficial de TinyCC (`make test`) falla exactamente en lo mismo con y sin el cambio: solo
`153_large_struct_args`, cuyo `.expect` no coincide tampoco con TinyCC original. Los demás cambios están en
`third_party/tinycc/README.SOMA`.

## Medidas

| | |
|---|---|
| `cc.elf` (dentro del núcleo) | 579 KB; ocupa 11,75 MiB al ejecutarse (2 segmentos, 3008 de 3072 páginas) |
| Hola mundo compilado | 86 KB; 9,2 MiB al ejecutarse con la memoria dinámica por defecto |
| Compilar un programa con su biblioteca | 12-15 ms en el ordenador de desarrollo |
| Batería `tests2` de TinyCC con el compilador y la biblioteca de SOMA | 110 de 112 |

En la batería `tests2` se cuenta como bien `153_large_struct_args`, porque su salida es idéntica a la de TinyCC original. Fallan
dos casos, y los dos son límites reales de SOMA: `101_cleanup` necesita 19 MiB para expandir sus macros y no cabe en un
proceso de 12 MiB; `119_random_stuff` usa `sys/mman.h`, y SOMA no tiene `mmap`.

## Pruebas

- **`make pruebas-cc`**, dentro de `make test`:
  - `tests/test_coder_c_265.c`: el programador en C con un LLM falso, 58 comprobaciones.
  - `tests/test_cc_265.py`: solo en Linux x86_64; en macOS avisa y no hace nada. Construye `cc.elf` con las mismas opciones
    del Makefile y ejecuta los **mismos ELF que cargaría el núcleo** con `tests/somaemu_265.c`. Ese emulador usa ptrace y
    atiende cada llamada al sistema como `usermode.c`, sin que ninguna llegue a Linux. Cada ejecutable se valida con
    `src/core/elf.c`. Hace 52 comprobaciones:
    - coma flotante idéntica a glibc;
    - archivos, entrada, memoria, `setjmp`, estructuras, varios archivos, ventana sin escritorio;
    - errores, fallos y programas demasiado grandes;
    - `cc -s` y `cc -x`, los ejemplos del programador, y las reglas de SOMA en todos los ejecutables.
- **`python3 tests/prueba_cc_265.py`**: en QEMU con la ISO construida; vale también en macOS. Hace 16 comprobaciones: la
  orden `cc` de verdad dentro de SOMA con el escritorio en marcha (compilar, ejecutar, archivos, errores, fallos y ventana).

## Archivos

- **Nuevos**:
  - `third_party/tinycc/`: TinyCC para x86_64 y `README.SOMA`.
  - `src/user/cc/`:
    - `cc.c`: el driver.
    - `libc.c`: la biblioteca.
    - `soma_sys.h`.
    - `cc_fs.py`: mete las cabeceras y la biblioteca dentro del compilador.
    - `include/`.
  - `tests/test_cc_265.py`, `tests/somaemu_265.c`, `tests/elfcheck_265.c`, `tests/cc_265/*.c`, `tests/test_coder_c_265.c`,
    `tests/prueba_cc_265.py`.
- **Cambiados**:
  - `Makefile`: reglas `cc.elf` y `ccprog.o`, y `pruebas-cc`.
  - `src/agent/shell_procesos.c`: `cmd_cc`, `shell_c_source` y el cerrojo compartido con Python.
  - `src/agent/shell.c` y `shell.h`.
  - `src/apps/coder_core.[ch]`.
  - `src/apps/coder_app.c`.
  - `CHANGELOG.md` y `README.md`.

Se quita con `python3 parche_265_compilador_c.py --revertir`, que deja todo exactamente como estaba.

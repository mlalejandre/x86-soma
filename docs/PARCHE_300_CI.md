# Parche 300 · La CI con el gcc 14 de GitHub

Las máquinas de GitHub ya compilan con **gcc 14**, que trata como error lo que gcc 13 solo avisaba. Por eso el parche 299
pasaba en Linux con gcc 13 y fallaba allí. Ahora se ha comprobado con gcc 14 (x86_64 y aarch64) y con clang. **Hace falta `make`.**

* `src/user/websvc.c`: incluye `tlscrypto.h`, porque llamaba a `sha256` sin declararla.
* `src/apps/web_f64.c`: `unpack()` no escribe nada si el número es 0. Quien la llama ya ha descartado ese caso, pero gcc 14 en
  aarch64 no lo ve y avisa de variables sin inicializar. Se inicializan a 0, y el resultado es el mismo.
* `Makefile` y `tests/test_vplay_252.py`: libxaac (el decodificador AAC de Android, de terceros) asigna funciones con
  `WORD32 *` a punteros que esperan `WORD16 *`. gcc 14 y clang lo dan como error aunque haya `-w`. Solo para libxaac se añade
  `-Wno-incompatible-pointer-types`; no se toca su código.
* El arranque de prueba (`make boot-smoke`) admite `BOOT_MAX_S`. En tu equipo el límite sigue en 6 s; la CI pasa
  `BOOT_MAX_S=30`, porque sus máquinas son compartidas y QEMU va sin KVM. Aquí, con la máquina ocupada, un arranque llegó a 8 s.

Comprobado aquí:
* `make ARCH=x86_64 WERROR=1 CC=gcc-14` y `make ARCH=aarch64 WERROR=1 CC=aarch64-linux-gnu-gcc-14`: compilan.
* `boot-smoke` arranca en las dos.
* `make ci HOST_CC=gcc-14`: pasa.
* `test_vplay_252` con gcc 14 y con clang: pasa.

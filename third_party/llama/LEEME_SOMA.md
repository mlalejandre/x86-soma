# llama.cpp en SOMA (parche 329)

Copia recortada de [llama.cpp](https://github.com/ggml-org/llama.cpp) (licencia MIT, `LICENSE`), commit `244640b`
(25 de noviembre de 2026): `include/`, `src/` (con todos los modelos de `src/models/`) y ggml solo con el backend de CPU
(`ggml/src/ggml-cpu`, con `arch/x86` y `llamafile`).

Cambios respecto al original:
* `src/llama-version.h` y `ggml/src/ggml-version.h`: escritos a mano (en llama.cpp los genera CMake).
* `ggml/src/ggml-backend-reg-soma.cpp`: registro de backends sin carga dinámica (solo la CPU), igual que en `third_party/whisper`.
* Nada más: el resto está tal cual. Lo que SOMA necesita (libc, C++, excepciones, base de FS) está en `src/user/ia/`.

Se compila como el programa de ring 3 `ia.elf` (ver el `Makefile`, `SOMA_329_IA_LOCAL`).

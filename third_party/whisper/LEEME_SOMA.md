# whisper.cpp en SOMA (parche 241)

Reconocimiento de voz (voz a texto) sin conexión: **whisper.cpp** + **ggml** (MIT, ver `LICENSE`), commit `5eaee0c`
de <https://github.com/ggml-org/whisper.cpp>. Solo están los archivos que hacen falta para el backend de CPU en x86_64.

El programa de SOMA que lo usa está en `src/user/voz/` (`voz.elf`, proceso de ring 3 con AVX2) y lo lanza la orden `voz`
(`src/arch/x86_64/voz_sys.c`).

## Cambios respecto al original (todos marcados con `GGML_SOMA` o `SOMA_241_VOZ`)

- `src/whisper.cpp`:
  - sin excepciones de C++: `throw std::runtime_error(...)` pasa a ser `WHISPER_SOMA_THROW(...)`, que termina el programa
    con el mensaje (`src/whisper-soma.h`). Los `try/catch` alrededor de la carga del modelo y de dos `resize` se quitan;
  - sin `std::regex`: la división en palabras de `tokenize()` la hace `whisper_soma_split_words()` (misma expresión de GPT-2,
    probada contra `std::regex` en `tests/test_voz_241.cpp`); `suppress_regex` no está disponible;
  - sin `std::ifstream`: los modelos se abren con `fopen/fread` (la libc de `voz` lee del almacén BIGFS);
  - sin el volcado de depuración `log_mel_spectrogram.json`.
- `ggml/src/ggml-backend-reg-soma.cpp` sustituye a `ggml-backend-reg.cpp`: solo el backend de CPU, sin `dlopen` ni
  `std::filesystem`. No se incluyen `ggml-backend-dl.h` ni `ggml.cpp` (el manejador de `std::terminate`).
- Los trozos opcionales que no se compilan (backend "meta", multiplicación "tiled", ANEForge de Apple) tienen funciones
  vacías en `src/user/voz/voz_cxx.cpp`.

## Compilación

`make` (x86_64) compila todo con `-fno-exceptions -fno-rtti -mavx2 -mfma -mf16c`, las cabeceras de libstdc++ del sistema
(con las *extern template* desactivadas por `src/user/voz/cxxinc/bits/c++config.h`) y la libc mínima
`src/user/voz/voz_libc.c`. De `libstdc++.a` solo se enlazan `tree.o` (árboles de `std::map`) y `hashtable_c++0x.o`.

`make voz-host ARCH=x86_64` crea la misma aplicación para Linux, para probar la calidad y la velocidad sin QEMU
(QEMU emula AVX2 muy despacio: unos 20 minutos para 7 segundos de audio).

## Modelos

Formato ggml de whisper.cpp. Recomendado: `ggml-base-q5_1.bin` (60 MB, SHA-1 `a3733eda680ef76256db5fc5dd9de8629e62c5e7`),
de <https://huggingface.co/ggerganov/whisper.cpp>. Más preciso y unas 3 veces más lento: `ggml-small-q5_1.bin` (181 MB).
Los modelos `.en` solo entienden inglés.

## Para actualizar whisper.cpp

Copiar los mismos archivos de la versión nueva y volver a aplicar los cambios de arriba (buscar `GGML_SOMA`). Si aparecen
símbolos sin definir al enlazar `voz.elf`, añadirlos a `voz_libc.c` (libc) o `voz_cxx.cpp` (C++).

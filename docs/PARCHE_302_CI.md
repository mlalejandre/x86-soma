# Parche 302 · La CI: macOS y fault-check

Solo cambian pruebas y la CI. **No hace falta `make`.**

Con el 300 pasaron Ubuntu y aarch64. Fallaban dos cosas:

* **Pruebas en macOS.** El SDK de Xcode 26 marca `sprintf` como obsoleto, y con `-Werror` eso es un error.
  * Cambian `tests/test_memoria_277.c`, `test_memoria_279.c`, `test_docgen_248.c`, `test_lang_264.c`, `test_datos_267.c`,
    `test_hwinfo.c` y `test_x509_194.c`: usan `snprintf` con el tamaño real del buffer.
  * Los programas de `tests/cc_265/` no se tocan, porque los compila el TinyCC de SOMA.
* **fault-check en x86_64.** El escenario de vídeo crea un AVI con `tools/mkavi.py`, que usa Pillow, y la máquina de GitHub
  no lo tenía. `ci.yml` instala ahora `python3-pil`.

Comprobado aquí: `make test STRICT=1`, compilando además cada prueba con clang como si `sprintf` estuviera marcada obsoleta
(como en el Mac), da TESTS OK.

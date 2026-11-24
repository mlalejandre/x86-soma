# Parche 297 · Listo para publicar en GitHub, con el correo y la ayuda

Solo documentación y publicación: no cambia el núcleo ni las aplicaciones, así que **no hace falta `make`**.

* `CHANGELOG.md`: entradas de los parches 285 a 297. De ahí salen las notas de la release que crea `subir_github.py`
  (busca los títulos que acaban en `(NNN)`).
* `README.md`:
  * las aplicaciones nuevas en la lista (chat SOMA, Correo, YouTube, Paint con IA, visor de documentos);
  * sección «Primeros pasos: la ayuda rápida (F1)» con las teclas;
  * sección «Correo con IA»;
  * los caminos del correo y de `investiga` en «Cómo decide SOMA el camino de una petición».
* `.gitignore`: `correo.conf`, `*.pem`, `*.key`, `*.p12` y `*.pfx` no se suben. El correo guarda la clave en `/etc/correo.conf`
  (dentro de la imagen, que ya no se sube); esto evita subir una copia suelta por error.
* `tools/exporta_para_claude.py`: temas `correo` y `ayuda`. Como siempre, nunca incluye `.conf`, `.pem` ni `.key`.

## Parche 296 · La prueba del correo compila con el clang nuevo de macOS

`src/apps/correo_core.c`: la tabla para quitar tildes era `char T[64]` con 64 letras, sin sitio para el cero final. El clang de
Xcode reciente lo da como error (`-Wunterminated-string-initialization`, con `-Werror`). Ahora es `T[]` y el tamaño lo pone el
compilador; el código que la usa solo lee las 64 primeras posiciones, así que nada más cambia.

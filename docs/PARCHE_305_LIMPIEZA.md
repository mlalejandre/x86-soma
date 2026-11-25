# Parche 305 · Limpieza: código y herramientas que ya no se usan

Primer paso de la auditoría de calidad: quitar lo que no se usa antes de seguir. No se pierde ninguna función de SOMA.
**Hace falta `make`** (sale un archivo del núcleo que nadie llamaba).

## Qué se quita (25 archivos, unos 5,9 MB)

| Archivo | Por qué |
|---|---|
| `in.mkv` (5,2 MB) | Vídeo de prueba olvidado en la raíz. Nada lo usa: las pruebas de YouTube crean sus `.mkv` en carpetas temporales. |
| `fuentes_ui.txt` (484 KB) | Volcado antiguo de `src/ui/` (anterior al parche 250). Nada lo lee, y al buscar en el proyecto aparece código viejo como si fuera el actual. |
| `tools/estructura*.py` (12) | Exportadores anteriores a `exportar_txt.py` (parche 236). Describen una arquitectura que ya no existe. **`estructura.py` exporta también los `.conf`**, así que podía volcar `llm.conf` o `correo.conf` con tus claves. |
| `tools/exportar*.py` (6) y `tools/exprotarmayordomo.py` | Lo mismo: exportadores por tema de antes de `exportar_txt.py --tema`. **`exprotarmayordomo.py` también exporta `.conf`**. |
| `src/apps/js_runtime.c` / `.h` | Primer «JavaScript» de juguete (buscaba `getElementById` con texto). Desde que existe el motor `web_js_*` nadie lo llama: el enlazador ya lo descartaba entero. |
| `docs/JAVASCRIPT_RUNTIME.md` | Describía un adaptador de QuickJS y una prueba (`tests/js_runtime_contract.c`) que no existen. |
| `src/agent/lang.h` | Copia exacta de `src/lang.h`. Ningún archivo de `src/agent/` la incluye. |

Para exportar el proyecto se usan `exportar_txt.py` (con `--tema` y `--parche`) y `tools/exporta_para_claude.py`. Los dos
filtran `.conf`, claves y tokens.

## Qué se corrige

* **`protected.txt`**: las cuatro líneas de los `lcm_*.inc` llevaban el comentario detrás del nombre
  (`src/agent/lcm_soma.inc   # SOMA_257_LCM`). `ejecutar.py` comparaba la línea entera, así que esas cuatro entradas no
  protegían nada. Hoy no había peligro real, porque `ejecutar.py` solo acepta parches de `.c`, `.h` y `.s`. Los comentarios
  pasan a su propia línea.
* **`ejecutar.py` y `tools/audit.py`**: al leer `protected.txt` ignoran lo que va después de `#`, para que no vuelva a pasar.
* **`exportar_txt.py`**: el tema `memoria` ya no nombra `tools/exportarmemoria.py`.
* **`Makefile`**: sin `js_runtime.o`.
* **Arreglo en `src/fs/srcfs.c`** (lo destapó la prueba `test_srcfs` al quitar la copia de `lang.h`): al buscar una fuente,
  el núcleo devolvía la primera entrada que coincidiera por el nombre entero **o** por el final de la ruta. Como
  `lang/lang.h` va antes que `lang.h` en el índice, `lang.h` daba el archivo equivocado. Hasta ahora pasaba por casualidad:
  la copia repetida `agent/lang.h` salía antes y era idéntica a `src/lang.h`. Ahora se busca primero la ruta exacta.
* **`.gitignore`**:
  * Fuera las líneas repetidas y las que empezaban con espacios (`   sd.img`), que git nunca aplicaba. Siguen cubiertas por `*.img`.
  * Nuevas: `*.mkv`, `fuentes_*.txt` y `estructura*.txt`, para que no vuelvan a subirse por error.

## Qué no se quita, aunque lo parezca

* `tools/desk_bench.py`: sigue funcionando. Mide la marca `[desk153]` cuando se compila con `-DSOMA_WEB_PERF_LOG=1`.
* `tools/sonda_youtube_nativo.py`: es el diagnóstico para cuando YouTube cambie (lo explica el parche 252).
* `workspace/*.soma`: son ejemplos de SOMA-LANG.
* `tools/lcm_ordenes/*`: son el entrenamiento y los exámenes del LCM.
* Las funciones que el enlazador descarta (unas 260 en x86_64): hoy no ocupan nada en el binario (`--gc-sections`, parche 199).
  Muchas son de la Raspberry Pi o las usan las pruebas. Se revisarán por módulos.

## Comprobado

* Sobre una copia limpia del commit `e46ee80`: aplicar → `--comprobar` → `make ci` (gcc 14) → `make ARCH=x86_64 WERROR=1` y
  `make ARCH=aarch64 WERROR=1` con gcc 14 → `boot-smoke` en las dos → `--revertir`. Tras revertir, el árbol queda
  idéntico byte a byte al original (`git status` limpio).
* `git check-ignore` da lo mismo antes y después para todos los archivos del proyecto, salvo las tres reglas nuevas.

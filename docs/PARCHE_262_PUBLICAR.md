# Parche 262 · Publicar: GitHub, README, exportador para Claude y tamaños del maquetador y el programador

Requiere el 261. Se aplica desde la raíz de SOMA:

```
python3 parche_262_publicar.py
python3 parche_262_publicar.py --comprobar
```

Para quitarlo: `python3 parche_262_publicar.py --revertir`, siempre antes que el 261.

## Subir a GitHub

Había tres problemas con los parches de un solo archivo (los `parche_NNN_*.py` con `--revertir`: 253 LCM, 257 a 262).

| Problema | Consecuencia | Arreglo |
|---|---|---|
| No se anunciaban: `subir_github.py` solo lee `parches/APLICADOS.txt`, y estos parches no escriben ahí | El commit habría dicho «actualización del día», la etiqueta se habría quedado en `.252` y la release, sin notas | Se reconocen por su respaldo `.parche_NNN_respaldo/`; el resumen sale del primer título de su `docs/PARCHE_NNN_*.md`. Si un número aparece en los dos sitios (el 253 es «audio nativo» y también «LCM»), se juntan los resúmenes |
| Se habrían subido sus respaldos: `.parche_*_respaldo/` no estaba en `.gitignore` (tienen copias de archivos antiguos), ni `para_claude_*.txt` ni los exportadores sueltos | Archivos locales en el repositorio público | Entran en `.gitignore`, y `subir_github.py` se niega a subirlos aunque alguien los quite de ahí |
| Las pruebas del LCM, el orbe y los tamaños no estaban en ningún sitio que se ejecutara solo | Un fallo en ellas no frenaba la subida | Objetivo **`make pruebas-lcm`** (unos segundos, sin Docker), dentro de `make test` y de la CI; `subir_github.py` lo pasa antes de tocar git |

`--sin-pruebas-rapidas` se salta esas pruebas. Con esto, `python3 subir_github.py --comprobar` debe decir:

```
nuevos desde la ultima subida: 253, 254, 255, 256, 257, 258, 259, 260, 261, 262
Etiqueta: v0.3.0-prebeta.262
```

## README y CHANGELOG

- **README:**
  - Secciones nuevas: **LCM** (qué es, ejemplos, seguridad, Ajustes → LCM, orden `lcm`) y **`llm-limites`**.
  - El orbe como teleprompter y su barra de fases.
  - El LCM en *Cómo decide SOMA el camino de una petición* y en la lista de agentes.
  - El exportador para Claude y la estructura del proyecto.
  - La voz (whisper y Piper) ya estaba documentada.
- **CHANGELOG:** entradas para el LCM (253) y para 257, 258, 259, 260, 261 y 262, con el formato `### Título (NNN)`. De ahí saca `subir_github.py` las notas de la release.

## Exportador para Claude

`tools/exporta_para_claude.py` sustituye a los `exporta_soma_NNN.py` sueltos:

```
python3 tools/exporta_para_claude.py
python3 tools/exporta_para_claude.py --tema lcm,orbe
python3 tools/exporta_para_claude.py --funcion lang_compile src/ui/gui.c
python3 tools/exporta_para_claude.py --temas
```

- Sin argumentos, recoge lo cambiado desde el último commit.
- `--tema` admite uno o varios temas separados por comas.
- `--funcion` añade el archivo donde se define esa función, y se puede combinar con rutas concretas.
- `--temas` lista los temas: lcm, voz, orbe, llm, agente, maquetador, programador, lang, github, compilar.

Siempre lleva el estado de git, los parches aplicados, los archivos de más de 20 MB (y si se subirían) y la tabla de órdenes. No incluye nunca `.conf`, claves ni binarios, y tapa tokens y credenciales en URL. `exportar_txt.py` (el proyecto entero por partes) sigue igual.

## Maquetador y programador

- **Maquetador** (`docagent.c`):
  - Con `llm-limites contexto`, el prompt de redacción crece como el de la respuesta directa; antes eran 8 KB fijos.
  - Las notas de investigación del buscador pasan de 3000 caracteres a un tercio del prompt.
  - La respuesta admite lo que diga `llm-limites salida`; antes, 160 KB.
  - Sin configurar nada, los tamaños de antes.
- **Programador:**
  - El prompt (`coder_core.c`) pasa de 24 KB a 80 KB. Lleva la referencia del lenguaje, un ejemplo y el programa actual entero (hasta 32 KB), así que al modificar un programa grande se cortaba, incluida la instrucción final «Responde SOLO con el programa COMPLETO».
  - El Studio (`coder_app.c`) lee, respalda y ejecuta programas de hasta 32 KB, como los que ya escribía el programador. Antes, uno de más de 16 KB no se podía modificar porque fallaba el respaldo.
  - Cuesta unos 120 KB más de memoria estática.
- **Lo que no cambia:**
  - El tamaño máximo de un programa sigue en 32 KB.
  - El compilador de SOMA-LANG genera como mucho 64 KB de bytecode (`g_bc`), y subirlo pide revisar la máquina virtual.
  - Los 7 intentos del programador y el `/no_think`.

## Medido y no medido

- **Probado:**
  - `make pruebas-lcm` con gcc (ASan y UBSan) y clang (`-Werror`): 675 casos, 38, 11, 57 y 26 comprobaciones, más la compilación de núcleo.
  - El exportador con un repositorio de prueba: cambiados, temas, funciones y tokens tapados.
  - `subir_github.py --comprobar` sobre una copia del árbol.
  - Aplicar y quitar sobre el árbol con 253 a 261.
- **No probado:**
  - El núcleo entero.
  - La subida real a GitHub. Hazla con `--comprobar` primero.

# Parche 301 · exportar_txt.py al día

Solo cambia `exportar_txt.py` (y el CHANGELOG). **No hace falta `make`.**

## Temas nuevos
| Tema | Qué lleva |
|---|---|
| `ia` | el agente, `agent_ruta` (LCM o modelo), el LLM, la memoria y `llm_server.py` |
| `lcm` | las órdenes y pesos del LCM, `tools/lcm_ordenes/`, sus pruebas y notas |
| `memoria` | la memoria infinita y sus exportadores |
| `chat` | la aplicación SOMA (chat) |
| `correo` | el correo, el orbe del correo, TLS 1.3 y sus pruebas |
| `ayuda` / `orbe` | la bienvenida y la ayuda F1; el orbe, la voz y los avisos |
| `investiga` | búsquedas, lectura de páginas e informe con fuentes |
| `youtube`, `paint`, `agenda`, `video` | cada aplicación con sus pruebas |
| `ci` | el workflow de GitHub, `audit.py`, arranque en QEMU, `fault_check.py`... |

* Los temas se pueden juntar: `--tema correo --tema ia`. Los archivos repetidos van una sola vez.
* `--temas` dice cuántos archivos y cuántos KB lleva cada tema.
* Si un tema pide notas de `docs/`, se incluyen sin `--con-docs`.

## Por parche
`python3 exportar_txt.py --parche 295` exporta todo archivo que lleve la marca `SOMA_295_...`, más `docs/PARCHE_295_*.md`.
Con `--parche 290-300` vale para un intervalo. Sirve para enseñarle al asistente exactamente lo que tocó un cambio.

## Cabecera
* Dice el último parche aplicado, que sale del CHANGELOG.
* Recoge las reglas que han salido en los últimos parches: escapes octales y sin símbolos débiles para el clang del Mac,
  ninguna llamada sin declarar (gcc 14), HOST_REF, que `--comprobar` no necesite el compilador del núcleo, y cómo es la CI.

## Secretos
* Nunca exporta:
  * `correo.conf`, `search.conf`, `token.json` y `.netrc`;
  * la memoria personal exportada (`memoria-export/`, `*.smem`);
  * modelos `.gguf`, bases de datos, vídeos (`.mp4`, `.mkv`), `.mp3`;
  * las copias de seguridad de los parches (`.parche_*`).
* Además de las claves que ya ocultaba, sustituye por `[CLAVE OCULTA]`:
  * los tokens `github_pat_`, `hf_`, `glpat-` y `AKIA…`;
  * cualquier bloque `-----BEGIN ... PRIVATE KEY-----`.

Comprobado: la exportación completa sale igual que antes (los mismos 754 archivos). Las claves de prueba quedan ocultas y
`correo.conf` aparece solo en la lista, sin su contenido.

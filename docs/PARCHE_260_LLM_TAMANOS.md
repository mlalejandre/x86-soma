# Parche 260 · Tamaños del diálogo con el modelo de lenguaje

Requiere el 259. Se aplica desde la raíz de SOMA:

```
python3 parche_260_llm_tamanos.py
python3 parche_260_llm_tamanos.py --comprobar
```

Para quitarlo: `python3 parche_260_llm_tamanos.py --revertir`, siempre antes que el 259.

## El problema

SOMA hablaba con el modelo usando búferes fijos, pensados como los de un controlador. Esto hacía que no se aprovechara el contexto del modelo, aunque el servidor admita 250 000 tokens, y que las respuestas largas se cortaran sin avisar.

| Qué | Antes |
|---|---|
| Petición HTTP | 64 KB |
| Respuesta HTTP | 64 KB (servidor local), 256 KB (otros proveedores), 1 MB (respuesta directa) |
| Texto de una respuesta | 32–48 KB |
| **Respuesta final** (`agent_last_response`: pantalla, voz, registro) | **8 KB ≈ 2 000 tokens** |
| Prompt del agente | 16 KB ≈ 4 000 tokens |
| Historial de pasos del agente | 8 KB |
| **Lo que el modelo veía de cada orden ejecutada** | **400 caracteres** |
| `max_tokens` | 8192 |

## Qué hace

Los tamaños salen de `/etc/llm_limites.conf`, que se maneja con la orden `llm-limites`:

```
llm-limites
llm-limites contexto 250000
llm-limites salida 16000
llm-limites como-antes
```

- `llm-limites` muestra los tamaños actuales.
- `llm-limites contexto 250000` indica la ventana de contexto de tu modelo (también vale `250k`).
- `llm-limites salida 16000` fija cuánto puede escribir el modelo de una vez (`max_tokens`).
- `llm-limites como-antes` vuelve a los tamaños de siempre.

Del contexto (C) y la salida (S), en tokens, se calcula todo lo demás. Se cuentan 3 caracteres por token, por debajo de lo real (el español da unos 4), y se reserva sitio para la salida:

| Qué | Cálculo | Con C = 250 000 y S = 8192 |
|---|---|---|
| Prompt del agente y de la respuesta directa | 3·C − 4·S − 4 KB, entre 16 KB y 8 MB | 700 KB |
| Historial de pasos | 3/5 del prompt | 420 KB |
| Salida de una orden | 1/8 del prompt, máximo 1 MB | 88 KB |
| **Lo que el modelo ve de cada orden** | 1/24 del prompt | **29 000 caracteres** |
| Texto de una respuesta | 5·S + 8 KB | 48 KB |
| Respuesta HTTP | 6 × el texto (por los `á` del JSON), como poco 1 MB | 1 MB |
| Petición HTTP | 2 × el prompt + 32 KB | 1,4 MB |
| Respuesta final | fija | 384 KB |
| Texto que muestra el orbe | fijo | 384 KB |

**Sin configurar nada,** el agente se comporta exactamente como antes:

- Siguen igual el prompt (16 KB), el historial (8 KB), los 450 caracteres de cada orden y `max_tokens` 8192.
- `max_tokens` llega al servidor byte a byte como antes; lo comprueba `tests/test_llm_proto_195.c`.
- Solo suben los techos: la respuesta final, la HTTP y el texto extraído. Así nada se corta antes de tiempo, pero ninguna petición cambia.

**Si una respuesta aún no cabe,** ya no se corta en silencio:

- Termina con «[... respuesta recortada: no cabía entera. Mira la orden llm-limites.]».
- Se avisa en la consola.
- `llm_recortada` vale 1.

**Si no hay memoria** para un búfer grande, se usa el de siempre (64 KB). El orbe, si no consigue 3 MB, prueba con la cuarta parte y luego con la dieciseisava.

## Cuidado con `salida`

`salida` no debe pasar del máximo de salida de tu modelo: Claude, OpenAI y Gemini rechazan la petición si se pasa.

Con un servidor local (llama.cpp), además, `contexto` no debe ser mayor que el `-c` con el que arrancaste el servidor. Si lo es, el servidor recortará o rechazará el prompt.

## Archivos

- `src/agent/llm_limites.inc` (nuevo, lo incluye `llm.c`): el cálculo, la lectura y escritura del archivo, y el aviso de recorte.
- `src/agent/llm_limites_shell.inc` (nuevo, lo incluye `shell.c`): la orden `llm-limites`.
- `src/agent/llm.c`:
  - Búferes según los tamaños; se rehacen con el turno del LLM tomado.
  - `max_body`, `max_tokens` y extracción del contenido sin topes de 48 KB.
  - Aviso de recorte.
- `src/agent/llm_proto.c` y `llm_proto.h`: `max_tokens` sale de `llm_proto_max_tokens`, que por defecto sigue siendo 8192.
- `src/agent/llm.h`: `struct llm_tam`, `llm_tam()`, `llm_tam_fija()` y `llm_recortada`.
- `src/agent/agent.h` y `agent.c`:
  - `agent_last_response` pasa de 8 KB a 384 KB.
  - La respuesta directa y el bucle del agente usan los tamaños calculados.
  - El cambio en el bucle reemplaza cada tamaño fijo por su variable; si tu `agent.c` difiere en algo de esa zona, el parche no se aplica.
- `src/agent/shell.c`: la orden en la tabla. `shell_agente.c` (opcional): la orden `llm`.
- `src/ui/gui_prompter.c`: el orbe acepta respuestas de 384 KB.
- `Makefile` y `tests/`.

## Medido y no medido

- **Probado en el anfitrión:**
  - `tests/test_llm_tam_260.c`: 26 comprobaciones sobre los cálculos, valores absurdos, el archivo con basura, el recorte sin partir letras UTF-8 y la orden.
  - `tests/test_llm_proto_195.c`: las peticiones salen idénticas a las de antes.
  - El orbe, con sus 57 comprobaciones.
  - Aplicar y quitar sobre un árbol con 253 → 259.
- **No probado:**
  - El núcleo entero (me faltan cabeceras).
  - Un servidor real con respuestas de cientos de KB.
- **Por verificar en tu equipo:**
  - Que `http_post_json`, el cliente del servidor local, admite más de 64 KB. No he visto su código; si tuviera un tope interno, una respuesta mayor daría «no se pudo leer la respuesta», igual que antes.
  - Que `wn_fetch` acepta `max_body` de varios MB.

Prueba sugerida:
1. `llm-limites contexto <el -c de tu servidor>`.
2. Pídele al orbe un texto muy largo, por ejemplo «escribe un relato de 6000 palabras».
3. Mira `/agent/traza.txt`, que guarda los caracteres de prompt y respuesta de cada llamada.

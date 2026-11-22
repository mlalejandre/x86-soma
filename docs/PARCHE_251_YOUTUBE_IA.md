# Parche 251 — YouTube con IA: buscador en lenguaje natural, transcripción y preguntas

Se apoya en el parche 250 (aplicación YouTube) y en dos piezas que SOMA ya tenía: el **LLM del sistema** (`llm_chat_ex`, el
mismo que configura `llm-config`) y **whisper** (`voz_run`, el reconocimiento de voz de la Grabadora).

## Qué añade

| Dónde | Qué |
|---|---|
| Caja de búsqueda | Con el interruptor **IA** encendido se escribe lo que se quiere ver con palabras normales. El LLM lo convierte en 1–3 búsquedas de YouTube, se juntan los resultados (sin repetidos, numerados) y el LLM escribe un **resumen de lo encontrado** con los vídeos que recomienda |
| Resultados | Cada tarjeta muestra el principio de la **descripción** del vídeo |
| Panel lateral del reproductor | Cuatro pestañas: **Lista** (la de antes), **Info** (fecha, vistas, idiomas con subtítulos y descripción completa), **Texto** (transcripción) e **IA** (resumen y preguntas) |
| Pestaña Texto | **Transcribir**: el sonido del vídeo pasa por whisper en SOMA. **Subtítulos**: se piden a YouTube los que tenga el vídeo (inmediato). El texto lleva marcas `[m:ss]`: pulsar una línea salta a ese momento. Ctrl+C lo copia |
| Pestaña IA | **Resumir el vídeo** y una caja para **preguntar por su contenido**. El LLM responde con la transcripción como contexto (o, si no se ha hecho, solo con título y descripción, y lo avisa) |

## Cómo funciona

```
"quiero aprender a hacer pan"  ──LLM──►  pan casero receta facil / pan de masa madre / ...
                                          │  (una petición /yt/search por búsqueda, intercaladas)
                                          ▼
                              resultados + descripción ──LLM──► resumen con recomendaciones (1), (3)...

Transcribir:  /yt/audio (WAV 16 kHz mono) ──► /yt_audio.wav en BIGFS ──► voz_run -T ──► "[m:ss] texto"  ─┐
Subtítulos:   /yt/subs (yt-dlp, json3/vtt)  ───────────────────────────────────────► "[m:ss] texto"  ─┤
                                                                                                        ▼
                       título + descripción + transcripción (recortada a `contexto`) + pregunta ──LLM──► respuesta
```

- Todo lo lento va en un hilo propio (`ytia`): un trabajo cada vez y uno en espera. La ventana nunca se queda parada y
  hay botón **Cancelar** (whisper se detiene con `voz_stop`).
- La transcripción se guarda en el almacén (`/yt_<id>.txt`): al volver a un vídeo ya transcrito aparece sola.
- El WAV temporal ocupa 32 000 bytes por segundo (1,9 MiB por minuto). Si no cabe en el almacén o el vídeo pasa del
  límite (`youtube minutos N`, 20 por defecto), se transcribe el principio y se indica.
- Si la transcripción no cabe en el contexto del modelo se envían dos tercios del principio y un tercio del final
  (`youtube contexto N`, 16 000 caracteres por defecto: súbelo si tu modelo tiene contexto de sobra).
- Lo que escribe el LLM se limpia antes de pintarlo (Markdown, emoji, comillas tipográficas).
- Sin LLM la búsqueda no se rompe: se busca el texto tal cual y la tarjeta dice por qué.
- Mientras se lee la ficha, el texto o la IA de un vídeo no se salta solo al siguiente al terminar.

## Servicio (`tools/yt_service.py`)

| Petición | Respuesta |
|---|---|
| `/yt/search` | Sexta columna: principio de la descripción (solo si la hay; un SOMA sin este parche la ignora) |
| `/yt/info?id=` | `SOMAYT 1 info`, líneas `campo<TAB>valor` (titulo, canal, duracion, vistas, fecha, subs), línea en blanco y la descripción |
| `/yt/audio?id=&max=<s>` | WAV PCM 16 bits, 16 kHz, mono, con `Content-Length` (se convierte entero antes de enviar). Plan B por tubería desde yt-dlp |
| `/yt/subs?id=&l=<idioma\|auto>` | `SOMAYT 1 subs <idioma> <0\|1 automáticos>` y el texto en párrafos `[m:ss]` cada ~20 s |

Elección de subtítulos: los hechos a mano en el idioma pedido; si no, los del idioma del vídeo (a mano o automáticos); si
no, inglés; las traducciones automáticas a otros idiomas solo como último recurso.

**Hay que reiniciar el servicio** si estaba abierto desde antes del parche (`ejecutar.py` avisa si encuentra uno antiguo).

## Órdenes nuevas

```
youtube ia on|off            buscador en lenguaje natural (también el interruptor IA de la barra, o "ia off" en la caja)
youtube ver <n>              abre el resultado número n
youtube transcribir          whisper sobre el vídeo abierto      youtube subtitulos    los de YouTube
youtube resumen              resumen del vídeo abierto           youtube pregunta <texto>
youtube idioma auto|es|en... idioma para whisper y subtítulos (auto = lo detecta)
youtube minutos 1..120       máximo de audio que transcribe whisper
youtube contexto 2000..120000  caracteres de transcripción que se pasan al LLM
youtube estado               incluye ahora el estado de la IA
```
Teclas en el reproductor: `1`–`4` cambian de pestaña, `i` va a la caja de preguntas.

## Requisitos y límites

- **LLM**: el que tenga configurado SOMA (`llm-config`). Con modelos locales pequeños conviene bajar `contexto`.
- **Whisper**: solo x86_64 con AVX2, modelo instalado y almacén BIGFS montado (como en la Grabadora). En QEMU sin AVX2 o
  en aarch64 el botón explica por qué no puede y **Subtítulos** sigue funcionando.
- Transcribir es lento (depende del modelo elegido en Ajustes) y compite por CPU con el vídeo: puede ir a saltos mientras.
- Los subtítulos automáticos de YouTube no llevan puntuación; el LLM se apaña bien con ellos.

## Archivos

- Nuevos: `src/apps/youtube_ia.c` (incluido desde `youtube.c`), `tests/test_youtube_ia_251.c`, `tests/test_yt_service_251.py`, este documento.
- Modificados: `src/apps/youtube.c`, `src/apps/yt_proto.h`, `tools/yt_service.py`, `Makefile`, `CHANGELOG.md` y
  `src/agent/shell.c` (**está en `protected.txt`**: solo cambia el texto de ayuda de la orden `youtube`, una línea).

## Verificado y no verificado

Verificado: compila en x86_64 y aarch64 con `WERROR=1`; `make test`; en QEMU, con un LLM de pega y el servicio en modo demo:
búsqueda en lenguaje natural con resumen, ficha, subtítulos, salto desde una línea, resumen, preguntas con historial, y la
descarga del sonido al almacén (19 MiB mientras se reproduce el vídeo).
**No verificado aquí**: whisper de verdad (el entorno de pruebas no tiene AVX2 ni el modelo), YouTube real (yt-dlp) y un LLM real.

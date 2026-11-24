# Parche 285 · La aplicación SOMA: un chat moderno con el agente

La ventana **SOMA** (el icono del agente en el dock) deja de ser un registro de texto con una línea `soma>` y pasa a ser una
aplicación de chat completa, al estilo de Open WebUI, pensada para hablar con el agente de SOMA.

## Qué hay en la ventana

**Panel lateral (historial)**
- *Nueva conversación*, búsqueda (en títulos y en todo el texto, sin mayúsculas ni tildes: «cumpleanos» encuentra «cumpleaños»).
- Conversaciones agrupadas: Fijadas, Hoy, Ayer, Últimos 7 días, Últimos 30 días, Anteriores. El icono dice el modo (bocadillo = Solo LLM, destello = Agente).
- Menú `···` de cada conversación: Renombrar, Fijar/Desfijar, Exportar a Markdown (se abre en Notas), Borrar.
- Abajo, el modelo en uso y su proveedor. El botón de la esquina esconde el panel (se esconde solo si la ventana es estrecha).

**Barra superior**: título de la conversación (clic = renombrar), insignia del modelo (punto verde; ámbar si el modelo no admite imágenes) y exportar.

**Mensajes**
- Tus mensajes en burbujas a la derecha, con sus adjuntos encima.
- Las respuestas de SOMA con su orbe, el modo (LLM / Agente), si usó la Memoria, la hora y lo que tardó.
- Acciones: copiar, regenerar, leer en voz alta (si hay voz instalada) y, en tus mensajes, editar y reenviar (la conversación sigue desde ahí).
- Mientras trabaja: «Pensando» con el tiempo; en modo Agente, la lista de pasos en vivo (entender la petición, mayordomo, búsqueda web, órdenes `$ …`, escribir…).
- Las respuestas aparecen con un barrido suave (los modelos no envían la respuesta a trozos en SOMA; ver «Límites»).
- Si algo falla, un aviso rojo con el motivo y **Reintentar**.

**Cuadro de texto**: crece con el texto (Intro envía, Mayús+Intro nueva línea, pegar textos largos), adjuntos con **+**, el selector de
modo **Solo LLM | Agente**, el interruptor **Memoria** y enviar / parar (Esc también para).

## Los modos

| | Solo LLM | Agente |
|---|---|---|
| Quién responde | el modelo configurado, directamente | el agente SOMA con sus herramientas (órdenes, web, archivos, documentos, programador…) |
| Contexto | toda la conversación (lo que quepa en el contexto del modelo, `llm-limites`) | la conversación en curso se añade al prompt del agente |
| Adjuntos de texto | van dentro de la pregunta (hasta 60 000 caracteres por archivo) | el agente recibe las rutas y los abre él |
| Imágenes adjuntas | al modelo, si tiene visión (si no, responde avisando) | rutas para el agente |

**Memoria** (por conversación): encendida, el chat consulta la memoria nueva (lo que sabe del usuario entra en el contexto) y el intercambio
se registra para que el sueño lo ordene; apagada, la conversación ni consulta ni deja recuerdos (también en modo Agente).

## Formato de las respuestas

Markdown de GitHub con LaTeX, dibujado por SOMA (`src/apps/chat_md.c` + el motor LaTeX de siempre, `tex.c`):
títulos, párrafos, **negrita**, *cursiva*, ~~tachado~~, `código`, listas anidadas y de tareas, citas y alertas (`> [!NOTE]`, `[!TIP]`,
`[!WARNING]`…), reglas, **tablas** con alineación, **bloques de código** con resaltado (C/C++, Python, JS/TS, shell, Rust, Go, Java, SQL,
JSON, HTML/XML, CSS…) y botón **Copiar**, **fórmulas** `$…$`, `\(…\)`, `$$…$$`, `\[…\]` y `\begin{equation|align}`, **enlaces**
(se abren en el Navegador; las URL sueltas también), **imágenes** `![](…)` locales (clic = Visor de imágenes) y de la web (se bajan en
segundo plano), entidades (`&amp;`, `&rarr;`, `&#233;`…) y HTML en línea (`<b> <i> <u> <s> <code> <kbd> <sup> <sub> <mark> <a href> <br>`).
El texto usa Liberation Sans (tiene los símbolos matemáticos que le faltaban a la fuente de la interfaz: ≈ ≤ ≥ ≠ ∞ ∫ ∑ ∈ ⇒ …).

## Dónde se guarda

- `/chat/historial.txt`: todas las conversaciones (se guarda 1,5 s después de cada cambio, nunca a mitad de una respuesta).
- `/chat/ajustes.txt`: modo, memoria y panel lateral por defecto.
- `/chat/conversacion-N.md`: las exportaciones.

## Cambios por debajo

- `llm_chat_hist` / `llm_proto_chat_hist`: conversación de varios turnos para los tres formatos (OpenAI/llama.cpp, Claude, Gemini),
  imágenes en la última pregunta, y se puede cortar (`cancel`). `llm_chat_ex` ahora es un caso particular y produce exactamente la
  misma petición que antes (comprobado byte a byte en los 4 proveedores, con y sin imágenes).
- Agente: `agent_chat_contexto(historial, sin_memoria)` y `gui_agent_submit_chat(mision)` (misiones largas: el orbe sigue limitado a 256).
  Las fases (`gui_fase`, `gui_fase_orden`) llegan también al chat. El orbe (tecla Windows) sigue funcionando igual.
- El campo de texto reutilizable gana `bare`, `tf_lines`, `tf_insert` y `tf_line_h` (los demás campos no cambian).

## Límites (honestos)

- **Sin streaming**: el cliente HTTP de SOMA (`wn_fetch`) devuelve la respuesta entera; por eso hay «Pensando…» y después el barrido.
- No es un navegador: el HTML que se entiende es el de las respuestas de los modelos (Markdown + etiquetas en línea); lo demás se quita y queda su texto.
- No se selecciona texto con el ratón dentro de las respuestas: se copia el mensaje entero o cada bloque de código.
- En modo Agente, las rutas especiales del agente (mayordomo, maquetador, programador) no usan el historial de la conversación, solo la pregunta.
- PDF adjuntos: en Solo LLM no se leen (usa el modo Agente).

## Pruebas

`make pruebas-chat` (dentro de `make test`):
- `tests/test_chat_285.c` (89): historial (ida y vuelta exacta del archivo, archivos cortados/estropeados, orden, grupos, búsqueda,
  contexto), las peticiones de varios turnos de los tres formatos, y el maquetador (bloques, zonas, resaltado, anchos de 120 a 900 px,
  límites de memoria y 3000 textos al azar con ASan/UBSan).
- `tests/test_chatapp_285.c` (106): la aplicación entera dibujada en el anfitrión con la fuente de verdad, usada con teclado y ratón:
  enviar, pensar, respuesta, enlaces y copiar código, segundo turno con historial, regenerar, editar, fallo y Reintentar, modo Agente con
  adjuntos y memoria apagada, fases en vivo, agente ocupado, imágenes local y remota, historial (buscar, fijar, renombrar, exportar,
  borrar), guardado y relectura, ventana estrecha, parar con Esc, pegar 6000 caracteres y 60 mensajes a 8 anchos.
  `./build/tests/test_chatapp_285 CARPETA` guarda 21 capturas.

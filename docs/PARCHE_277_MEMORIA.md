# PARCHE 277 · La memoria de SOMA pasa a ser memorIA (núcleo en C)

Sustituye la memoria anterior del agente (ventana de 8 intercambios `STM` + árbol `/mem/*.txt` con búsqueda por palabras)
por la de memorIA: **hechos** atómicos extraídos por el LLM, **mensajes literales**, búsqueda por **embeddings**, fechas,
sustitución temporal y regla de atribución. Es el primer paso de tres (278: servidor de embeddings desde SOMA; 279: árbol de nodos y ciclo de sueño; 280: importar el
cartucho real de memorIA y enlazar el calendario).

## Qué cambia para el usuario
* Todo lo que dices al agente se guarda literal (con fecha) y, en segundo plano, el LLM extrae hechos sueltos
  («Mi cumpleaños es el 3 de marzo», «Trabajo en X»). Si un hecho nuevo cambia uno antiguo (cambio de trabajo, de casa…), el
  antiguo queda como *sustituido* y no se vuelve a dar como vigente.
* En cada misión, el prompt lleva: los hechos más parecidos a lo que preguntas, mensajes literales relacionados y los últimos
  mensajes de la charla, con la regla de atribución de memorIA (cada recuerdo dice de quién es; si no consta, se dice).
* Orden nueva `memoria`: `estado | recordar TEXTO | buscar CONSULTA | ver N | olvidar N | probar | config | importar | guardar | ayuda`.
* `mem_search`, `mem_write`, `mem_read N`, `mem_tree` del agente usan la memoria nueva (`mem_write` guarda un hecho; `mem_read` con un número
  lee ese recuerdo; `mem_tree` muestra el estado). Los archivos antiguos de `/mem` no se tocan; `memoria importar` los carga como hechos.
* `olvidar` **archiva** (reversible con `ver`); nada se borra.

## Cómo funciona
* `src/agent/memoria_core.[ch]`: lógica pura, sin kernel (se prueba en el anfitrión con ASan/UBSan). Vectores int8 (el kernel no usa
  coma flotante), coseno por producto escalar entero, canal lexical con peso idf cuando no hay vector (servidor caído o elemento sin
  embedding), parsers de las respuestas del LLM, prompts, contexto y guardado por fragmentos de 1024 elementos con CRC32.
* `src/agent/memoria.c`: pegamento. Config en `/etc/memoria.conf`; embeddings por `POST /v1/embeddings` (por defecto, la IP del servidor
  LLM local, puerto 8089, modelo `bge-m3`, como memorIA); hilo en segundo plano que extrae hechos **solo cuando ningún agente usa el
  LLM** (≥4 s de calma), rellena embeddings pendientes y guarda `/memoria/mNNN.bin` cada 5 s si hubo cambios.
* Si el servidor de embeddings no contesta, la memoria sigue funcionando por palabras y reintenta a los 30 s; los elementos sin vector
  se embeben después.
* `agent.c`: `stm_append_interaction` → memoria; `inject_agent_memory` → `memoria_contexto`; `agent_run` registra entrada/salida de agente.
  Las sub-misiones delegadas no se guardan como mensajes del usuario. `/etc/mem_user.txt` y `/etc/mem_hw.txt` siguen inyectándose.

## Límites (sin maquillar)
* Capacidad: 40 960 elementos (40 fragmentos; el VFS admite 256 archivos). Sin ciclo de sueño aún, así que no se compacta nada hasta el 279 (árbol y ciclo de sueño; el 278 es el servidor de embeddings). <!-- SOMA_279_SUENO -->
* Extracción y comparación de hechos dependen del LLM que tenga SOMA; no se ha medido su calidad frente al LLM de 35B con el que se midió
  memorIA (69,6 % en LoCoMo, 99,1 % en corpus propio). Esas cifras NO son de esta implementación.
* La llamada de embeddings y la del LLM comparten el cerrojo de red: un mensaje nuevo mientras se extrae un hecho espera a que termine.
* Mensajes literales guardados hasta 1200 bytes; en el prompt se muestran 500.
* No probado aquí: arranque en QEMU, HTTP real contra bge-m3, hilo y persistencia en disco. Sí probado: núcleo (147 comprobaciones), compilación
  de todo el kernel sin avisos, aplicar/comprobar/revertir el parche.

## Pruebas
`make pruebas-memoria` (también dentro de `make test`). Necesita `make` para compilar el kernel; `make test` no es imprescindible.

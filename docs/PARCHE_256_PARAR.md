# Parche 256: Esc detiene la misión y la lectura en voz alta

## Qué fallaba (revisión)
- La misión del agente corre en el hilo `soma_agente` y espera al modelo en una petición HTTP bloqueante; no había botón, tecla ni
  orden para pararla. Escribir otra misión respondía «Sigo trabajando en la misión anterior».
- La lectura en voz alta solo se podía parar con el botón del Bloc de notas o `habla parar`; `voz_speak_stop()` no hacía nada mientras
  se preparaba (escribir el texto, cargar la voz) y no cubría `habla texto` (síncrono, bloquea al agente).
- Esc en el escritorio salía del escritorio.

## Qué hay ahora
- `gui_agent_cancel()` (`gui_agente.c`): para la lectura si suena, mata el hilo del agente (`thread_kill`: cierra sus conexiones TCP y
  libera sus cerrojos; el cambio de hilo es cooperativo, así que no queda dentro del montón), reinicia la petición de pantalla
  (`g_gui_req/grant/depth`), pone «Mision detenida.» como respuesta y marca la misión terminada: el bucle principal la cierra como siempre.
- `her_stop_escape(agent_ok)`: Esc para la lectura (`voz_speaking()`); si no, y con el orbe/escritorio en foco, la misión. Se llama
  tras `her_voz_escape()` (el dictado tiene prioridad) y, en el Bloc de notas, después de `notepad_escape()`.
- `voz_sys.c`: `g_sp_cancel` (parada durante la preparación: se comprueba antes de lanzar el proceso), `g_sp_proc` y `voz_speaking()`.
- Aviso en el orbe (`gui_cosmos.c`): «Esc detiene la misión» / «Esc detiene la lectura en voz alta».

## Límites
- Cancelar la misión pierde los búferes de `agent_run` (~200 KiB por cancelación) y puede dejar un archivo a medias o un proceso de
  usuario lanzado por el agente (salvo la lectura) sin cerrar. Una cancelación limpia exigiría una bandera en `agent.c`/`llm*.c` (protegidos).
- Un Esc con la lectura sonando y la misión en marcha para primero la lectura; otro Esc, la misión.

## Archivos
`src/ui/gui.c`, `src/ui/gui_agente.c`, `src/ui/gui_cosmos.c`, `src/arch/x86_64/voz_sys.c`, `src/arch/x86_64/voz_sys.h`. Ningún archivo protegido.

## Sin verificar
La parada de la voz en QEMU (no hay voz de Piper instalada en el entorno de pruebas); la misión sí se probó con un modelo falso lento.

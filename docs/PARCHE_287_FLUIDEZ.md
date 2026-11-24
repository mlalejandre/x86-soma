# Parche 287 · El orbe ya no va a tirones mientras SOMA trabaja

## Qué pasaba

El planificador del núcleo es **cooperativo**: un hilo solo deja paso a otro cuando espera, duerme o cede la CPU. Mientras el agente trabajaba
(en el hilo `soma_agente`), escribía mucho en la consola: la respuesta completa del LLM, la salida de las órdenes, el «DICTAMEN FINAL»…
Cada carácter de `kprint` hacía dos cosas lentas:

1. **El puerto serie**: espera en bucle a que la UART esté libre, sin ceder la CPU.
2. **La pantalla de texto VGA** (`0xB8000`), que con el escritorio gráfico ni se ve. En QEMU cada escritura ahí es una salida de la
   máquina virtual, y cada salto de línea desplaza 4000 bytes uno a uno.

Unos KB de respuesta dejaban la GUI sin CPU durante décimas de segundo o más. Por eso el orbe y su animación se paraban a trompicones justo
cuando la pregunta estaba en marcha.

## Qué cambia (`src/ui/console.c`)

- Con el escritorio gráfico activo, `kputc` ya no escribe en la pantalla de texto VGA. El registro (`klog`) y el puerto serie siguen
  recibiéndolo todo, así que no se pierde nada de las trazas.
- `kprint` cede la CPU cada 128 caracteres, como mucho una vez cada 4 ms, pero solo desde el hilo del agente y con las interrupciones
  activas (nunca dentro de un manejador ni de una sección crítica). Así la GUI dibuja sus fotogramas mientras el agente escribe.

Nota: al salir del escritorio a la consola de texto, lo que se imprimió mientras estaba el escritorio no aparece en la pantalla de
texto. Sigue en el registro y en el puerto serie.

Hace falta `make`.

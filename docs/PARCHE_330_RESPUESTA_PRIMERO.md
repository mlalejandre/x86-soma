# Parche 330 · La respuesta de verdad antes que la muletilla (tarea 2)

**Hace falta `make`.**

## El problema
Al hablar con SOMA, antes de ir al modelo de lenguaje el LCM dice en voz alta lo que va a hacer («Esto se lo paso al modelo de
IA: lo piensa y te contesto»). Pasaban dos cosas:

1. **Se decía siempre**, aunque el modelo respondiera en un segundo, y siempre parecido: en una conversación seguida era monótono.
2. **A veces no se cortaba.** La respuesta de verdad debía cortar la lectura, pero si la orden de parar llegaba mientras se
   preparaba el aviso (descomprimir y lanzar el programa de voz, cargar la voz), el proceso todavía no estaba apuntado, la parada
   se perdía y el aviso sonaba entero antes de la respuesta.

## Qué cambia
* **El aviso hablado espera 2,5 s** (`src/ui/gui_agente.c`). Si en ese tiempo llega la respuesta, el aviso no se dice. Con un
  modelo rápido, lo normal es que solo se oiga la respuesta.
* **La misma frase no se repite** si se dijo hace menos de 2 minutos. En el orbe y en la conversación el aviso sale siempre
  (no estorba y dice cuánto suele tardar).
* **La respuesta corta la lectura al momento** (`src/arch/x86_64/voz_sys.c`):
  * se para el proceso de voz y **se vacía la cola de la tarjeta de sonido** (`audio_sys_flush`), sin esperar a que acabe la frase;
  * una parada que llega mientras se lanza la lectura ya no se pierde: el proceso se para nada más arrancar;
  * si a los 1,5 s algo sigue sonando, se vuelve a parar.
* Lo mismo vale para las frases que dice el programador mientras trabaja: salen a los 2,5 s solo si sigue trabajando.

## Pruebas
* Compila sin avisos en x86_64 y aarch64; `make ci`, `sbx-check` y `fault-check`.
* La lectura en voz alta necesita una voz de Piper, que no puedo descargar aquí. El cambio es de orden y de tiempos (cuándo se
  manda leer y cuándo se corta), sin tocar la síntesis. Pruébalo así: pregunta algo sencillo y la respuesta debe sonar sin
  aviso previo; pide algo largo (un documento) y el aviso debe sonar a los 2,5 s y cortarse en cuanto llegue la respuesta.

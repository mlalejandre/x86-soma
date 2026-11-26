# Parche 333 — El programador ya no abre el Terminal al ejecutar

## Antes
«Ejecutar» en SOMA Studio, con un programa en Python, C o JavaScript, abría la ventana del Terminal encima del programador
y la dejaba abierta al terminar; con un juego que tiene su propia ventana, además, el Terminal se quedaba vacío y molestando.

## Ahora
- El programa corre en su propio hilo y lo que imprime sale en el panel **Salida**, debajo del editor: nombre del programa,
  si sigue en marcha (con el tiempo que lleva) o cuánto tardó, y si se paró a mano.
- Si el programa pregunta algo (`input()`, `scanf`), se escribe en la línea del panel y se envía con Intro; la pregunta y
  la respuesta quedan en la salida, como en un terminal.
- **Esc** o «Parar» lo detienen; «Otra vez» lo relanza; «Cerrar» (o Esc) oculta el panel.
- Si el programa no imprime nada (un juego con su ventana), el panel se cierra solo al terminar.
- Las páginas HTML se abren directamente en el navegador, sin pasar por el Terminal.
- La salida se sigue recogiendo aunque el programador no esté delante (por ejemplo, con la ventana del juego activa).
- Se conservan las últimas 400 líneas (la rueda del ratón sube por ellas); los colores ANSI se quitan y las líneas muy
  largas se parten sin cortar caracteres.

El Terminal sigue igual para quien quiera usarlo (`python script.py`, `cc -run`, `js`).

## Pruebas
- `test_coder_run_333` (en `make test`): líneas, saltos `\r\n`, colores ANSI partidos entre trozos, tabuladores, control,
  líneas largas con UTF-8, las 400 últimas en orden y 20000 trozos de basura.
- `python3 tests/prueba_salida_333.py` (QEMU): un programa Python que imprime, pregunta y responde; uno que no imprime nada
  (el panel se cierra solo) y uno que no termina (se para).
- Necesita `make`.

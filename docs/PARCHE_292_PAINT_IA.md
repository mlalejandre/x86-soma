# Parche 292 · Dibujar en Paint con la IA

## Qué hace

Paint dibuja lo que le pidas con el modelo de lenguaje, y lo pinta **paso a paso, del fondo a los detalles**, como si lo dibujara a
mano. El dibujo queda en el lienzo como cualquier otro: lo retocas con el lápiz, el cubo o las figuras, lo guardas con Ctrl+S o lo
quitas con Ctrl+Z.

### Desde Paint

1. Pulsa el botón **IA** de la barra (junto al zoom).
2. Escribe qué quieres en el campo («un faro al atardecer», «un gato naranja durmiendo»…) y pulsa **Enter**. Esc cierra el campo.
3. La barra de estado indica qué hace en cada momento: «La IA está pensando el dibujo de un faro al atardecer (12 s)…» y luego
   «Dibujando… 3 / 8».

### Desde el orbe o el chat

Funcionan frases como «dibújame un faro al atardecer», «pinta un gato naranja en paint», «haz un dibujo de un dragón» o «¿puedes
dibujar en paint una casa?».

SOMA avisa de lo que va a hacer, abre una ventana de Paint, lo dibuja y al terminar lo dice. No cuentan como dibujos «abre paint»,
«pinta la pared de azul», «dibuja un gráfico de ventas», «dibuja un diagrama de flujo», «cómo se dibuja un caballo» ni «dibuja el
árbol de archivos». Las 2344 frases del catálogo del LCM siguen yendo a donde iban.

## Cómo funciona

- **El modelo devuelve un SVG** de 640×480: el fondo primero y después cada objeto en su grupo, de atrás hacia delante, con
  degradados y contornos. No usa texto, imágenes ni filtros.
- **Se comprueba antes de pintarlo.** El rasterizador de SVG del navegador lo dibuja en pequeño; si no hay SVG, tiene errores o sale
  vacío, se le pide otra vez una sola vez, diciéndole qué falló.
- **Se pinta por pasos** (`src/apps/paint_svg.c`). El SVG se trocea en sus elementos de primer nivel, hasta 14 pasos. Cada paso pinta
  un poco más del dibujo, unos 110 ms después del anterior, y el último es el dibujo entero.
  - El rasterizador es el del navegador y corre en el proceso aislado `imgdec`, como las imágenes de las páginas.
  - El dibujo se mezcla con el lienzo respetando las transparencias.
- **El lienzo:**
  - Si estaba sin tocar, pasa a 640×480.
  - Si ya había un dibujo, se pinta encima sin cambiar el tamaño.
  - Todo el dibujo cuenta como un solo paso de deshacer.
  - Mientras pinta, no deja dibujar encima.
- **Un dibujo a la vez**, y es de su ventana de Paint. Si hay dos ventanas, el dibujo va a la que lo pidió. Si se cierra la ventana a
  medias, se descarta.
- **La GUI repinta esa ventana sola** mientras dibuja, aunque no muevas el ratón.

## Archivos

- `src/apps/paint_svg.c` y `paint_svg.h`: lógica pura (extraer el SVG, trocear, pasos, mezclar, el tema de la petición).
- `src/apps/paint_ia.inc`, incluido en `paint.c`: el hilo `paint_ia`, el modelo, los pasos, el botón, el campo y los avisos.
- `src/apps/paint.c` y `paint.h`: el botón IA, el campo, la barra de estado y la API `paint_ia_encargar`, `paint_ia_estado`,
  `paint_animando` y `paint_ia_slot`.
- `src/agent/agent_ruta.c`: `ar_dibujo`, que decide qué peticiones del orbe son dibujos.
- `src/agent/agent.c`: la ruta del orbe (aviso, abrir Paint, esperar y contar el resultado).
- `src/ui/gui.c`: el repintado automático mientras dibuja.

## Pruebas

- `tests/test_paint_ia_292.c` (53 comprobaciones):
  - sacar el SVG: con razonamiento, con ```svg, cortado o sin SVG;
  - trocear: comillas con `>`, comentarios y CDATA;
  - los pasos, con el rasterizador de verdad: cada uno cubre al menos lo del anterior, el último es idéntico al dibujo entero y el
    primero ya tiene el fondo con su degradado;
  - la mezcla, el tema de la petición y qué frases del orbe son dibujos.
- `tests/test_paint_ia_maquina_292.c` (34 comprobaciones): Paint entero con el rasterizador de verdad y el modelo simulado.
  - El botón, el campo, el dibujo por pasos y el lienzo final idéntico al dibujo.
  - El Ctrl+Z que lo quita entero y el Ctrl+Y que lo devuelve.
  - Pintar encima de un dibujo, el segundo intento, el dibujo vacío y la falta de modelo.
  - Un encargo a la vez, dos ventanas y cerrar a medias.
  - El repintado automático.

  Las dos pasan con ASan, también con detección de fugas. Esta prueba encontró un fallo real en la primera versión: con el lienzo
  sin tocar, el dibujo no se podía deshacer. Está corregido.

Hace falta `make`.

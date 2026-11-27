# Parche 347 · El navegador, más cerca de Chrome en webs reales

**Hace falta `make`.** Requiere el 346.

Como en el 346, todo sale de comparar SOMA con Chrome en la misma página y con el mismo ancho de ventana. El artículo de
20minutos.es ha sido el caso de referencia. Para cada diferencia se buscó la causa en el motor, se redujo a un caso mínimo y se
comparó con Chrome. Las soluciones son generales: valen para cualquier web que use lo mismo.

## 1 · `Intl` de verdad

`Intl` era un objeto casi vacío: `Intl.DateTimeFormat().resolvedOptions()` no existía y el JavaScript de 20minutos fallaba al
empezar. Ahora están, en español y en inglés:

* `DateTimeFormat` (con `format`, `formatToParts` y `resolvedOptions`) y `NumberFormat` (moneda, porcentaje, compacto);
* `Collator`, `PluralRules` (cardinal y ordinal) y `RelativeTimeFormat` («ayer», «hace 3 minutos», «in 2 hours»);
* `ListFormat` («a, b y c»), `Segmenter`, `DisplayNames` y `Locale`;
* `getCanonicalLocales` y `supportedValuesOf`.

Los constructores funcionan también sin `new`, como en el estándar.

## 2 · Rejilla CSS: líneas con nombre y `auto-fill`

La plantilla de 20minutos es
`[main-start content-start] repeat(auto-fill, minmax(60px, 1fr)) [content-end sidebar-start] 300px [sidebar-end main-end]`.

* **Líneas con nombre.** `grid-column: content-start / content-end` funciona, con nombres que vienen de un `repeat()` o de un área
  (`nombre-start` y `nombre-end`), y también nombres sueltos en `grid-area`.
* **`auto-fill` con pistas fijas.** Se repite el patrón cuantas veces quepa *junto a* las pistas fijas. Antes todas las pistas se
  convertían en `1fr` y la de 300 px desaparecía.
* **Mínimo de `minmax(60px, 1fr)`.** El mínimo de esa pista son los 60 px, no el contenido; el contenido solo cuenta con `1fr` a
  secas. Un elemento colocado solo en la primera columna la ensanchaba a su ancho mínimo de contenido (148 px), y el artículo
  entero quedaba 80 px más ancho que en Chrome.

Resultado: titular, cuerpo y barra lateral miden lo mismo que en Chrome (990, 670 y 300 px).

## 3 · Orden de los scripts `defer` y de los módulos

Los `<script defer src>` y los módulos sin `async` se ejecutaban en orden de documento con `readyState` en `"loading"`. En Chrome
se ejecutan al terminar de analizar el documento, con `readyState` en `"interactive"` y antes de `DOMContentLoaded`.

El patrón `"loading" !== document.readyState ? r() : addEventListener("DOMContentLoaded", r)` (20minutos) acababa llamando a
`r(evento)`, y fallaba con «querySelectorAll no es una función». Ahora hay dos fases. `readystatechange` se dispara una sola vez
por estado.

## 4 · `setHTML`, `setHTMLUnsafe` y `Sanitizer`

Chrome ya tiene la API Sanitizer y el SDK de consentimiento de 20minutos llama a `setHTML` sin comprobar que exista. Ahora están:

* `Element.setHTML` y `ShadowRoot.setHTML`. Quitan `<script>`, `<iframe>`, `<object>`, `<embed>`, `<base>`, los atributos `on*` y
  las URL `javascript:`.
* `setHTMLUnsafe`, `Document.parseHTML` y `Document.parseHTMLUnsafe`.
* `Sanitizer`.

En `<style>`, `<textarea>` y `<title>` el contenido se pone como texto.

## 5 · Errores con su pila

Los errores sin capturar de una tarea, una promesa o un temporizador salen con las primeras líneas de su pila. En el registro del
navegador, «Uncaught TypeError» dice además en qué función ocurrió. Solo se añade la pila si nombra alguna función: un solo
`at script:1` no aporta nada a `(l:1)`.

## 6 · `querySelectorAll` en la orden `js`

El documento de la orden `js` (y de sus pruebas) no tenía hoja de estilo, y `querySelector(All)`, `matches` y `closest` no
encontraban nada. Ahora la tiene.

## 7 · Imágenes de fondo e iconos

* **SVG sin tamaño propio.** Un SVG con `viewBox` y sin `width`/`height` se encaja como `contain` en la caja del fondo (CSS
  Images 3). Antes se pintaba a 512×512 en una caja de 13 px y solo se veía una esquina transparente: así faltaban los iconos de
  reloj y de autor.
* **Fondo de elementos en línea.** `background-image` en un elemento en línea se pinta; antes solo se pintaban el color y el
  degradado.
* **SVG y WebP sin pasar por PNG.** Las descargas de imágenes anuncian `image/webp` e `image/svg+xml`, que SOMA dibuja desde el
  180. Con `soma.webimg=off` se siguen pidiendo solo PNG, JPEG y GIF. La pasarela (`tools/web_gateway.py`) guarda el SVG original
  y solo lo convierte a PNG si el cliente no anuncia `image/svg+xml`. Antes lo convertía siempre, a su tamaño de `viewBox`, y se
  perdía que no tenía tamaño propio.
* **`var(--x)` en un `<svg>` en línea.** `stroke="var(--primary-color)"` se resuelve con las variables del elemento: el icono de
  comentarios de 20minutos no salía.

## 8 · Bordes redondeados que tapaban el fondo

Un borde redondeado se dibujaba como un rectángulo redondeado del color del borde y, encima, otro relleno con el color de fondo.
Eso tapaba la imagen de fondo (la «G» del botón «Añádenos en Google»), los degradados y lo que hubiera bajo un fondo transparente.

Ahora, si el fondo no es un color opaco, el interior se guarda antes y se restaura después: solo los píxeles dentro de la curva
interior. Con fondo opaco sigue el camino rápido de antes.

## 9 · Texto suelto dentro de un contenedor flex o grid

El elemento anónimo que envuelve el texto suelto de un contenedor flex o grid compartía el estilo del contenedor. Tomaba su
`height`, `flex`, `order`, `align-self`, `min`/`max` y su colocación en la rejilla. Con `height:30px` y `align-items:center`, el
texto quedaba arriba (botón «Añádenos en Google»).

Ahora el anónimo tiene una copia del estilo con esas propiedades en su valor inicial. Hereda solo lo heredable: texto, color y
alineación.

## 10 · Sin viñeta en los `<li>` de un contenedor flex o grid

Como en Chrome y Safari, `ul { display: flex }` quita los puntos: un `<li>` que es elemento de un contenedor flex o grid no lleva
marcador.

## 11 · Fuentes con serifa (Liberation Serif)

SOMA solo tenía fuentes sin serifa, y los titulares y textos con serifa salían en sans: más anchos, y cortaban en otras palabras.
Ahora incluye Liberation Serif regular y negrita (`src/apps/web_libserif.ttf` y `web_libserifb.ttf`, unos 340 KB):

* misma licencia que la Liberation Sans que ya llevaba (SIL OFL 1.1; aviso en `docs/LICENSE_LIBERATION_SERIF.txt`);
* mismo subconjunto y sin hinting;
* mismos anchos que Times New Roman, que es lo que usa Chrome en Linux.

`font-family` se lee como en el navegador: se toma la primera familia reconocible y se saltan las fuentes web que SOMA no tiene.
Así, `Source-Serif, serif` es serifa y `Roboto, serif` es sans (Roboto es sans). `Georgia`, `Times`, `Merriweather`, `Lora`,
`Playfair`… son serifa. Sin ninguna familia reconocible, sans, como siempre en SOMA.

Además, `font-weight` mayor que 500 (599, 650…) es negrita: con solo regular y negrita disponibles, eso es lo que elige el
estándar. Antes solo valía 600, 700, 800 y 900.

## 12 · `inherit` y `unset` como en el estándar

* **`inherit` en más propiedades.** `*, *::before, *::after { box-sizing: inherit }` con `html { box-sizing: border-box }` es el
  patrón de Bootstrap y de muchos temas de WordPress. En SOMA el `inherit` de `box-sizing` se perdía: todo quedaba en
  `content-box` y las cajas con relleno salían más grandes. El menú de El Bierzo Digital medía 1503×924 en vez de 1253×800. Ahora
  `inherit` vale también en `box-sizing`, la familia y el estilo de letra, `font`, `text-transform`, `white-space`,
  `letter-spacing`, `list-style`, `cursor`, márgenes, rellenos, `min`/`max`, `overflow`, `position`, `opacity`, `z-index`…
* **`unset`.** Es `inherit` solo en las propiedades heredadas (color, letra, alineación…) e `initial` en las demás. Antes era
  `inherit` en todas: `width: unset` copiaba el ancho del padre.

## 13 · Flex: base acotada por `min` y `max`

El tamaño hipotético de un elemento flex es su base acotada por `min-width` y `max-width` (CSS Flexbox 9.2). Con él se reparten
las líneas y el espacio libre. SOMA usaba la base sin acotar: un elemento con `min-width: 30%` y poco contenido medía su
contenido, 66 px en vez de 370 en la barra de El Bierzo Digital.

## Herramienta de comparación

Para encontrar estas diferencias en cualquier web sirve `cmpweb.py`, una herramienta de trabajo que no va en el parche. Maqueta
la misma página sin JavaScript con el motor de SOMA en el anfitrión y en Chrome:

* las hojas de estilo enlazadas van en línea;
* las imágenes miden lo mismo en los dos;
* los `<noscript>` se quitan.

Después lista los elementos cuya caja difiere en más de 4 px, de arriba abajo. Las diferencias que quedan en las webs probadas son
sobre todo de fuentes web (WOFF2, que SOMA no carga) y de SVG en línea.

## Pruebas

* `test_web_bgimg_347` (nueva): SVG sin tamaño, fondo en línea, botón con posición de 4 valores, degradado y fondo transparente con
  borde redondeado, y texto suelto centrado en flex. Falla 5 de 8 con el código del 346.
* `test_web_serif_347` (nueva): anchos de Times New Roman, de una fuente web desconocida con `serif` y de Georgia negrita, igual
  que en Chrome; `sans-serif` sigue siendo sans.
* `test_web_layout_181`: la rejilla de 20minutos (líneas con nombre, `auto-fill`, pista de 300 px y mínimo fijo); `box-sizing:
  inherit`, `width: unset` y `position: fixed` de `100vw` con relleno; y flex con `min-width: 30%` y con `max-width`.
* `jsrun_host_274`: «intl 347» y «sanitizer 347» (con `querySelectorAll` sobre fragmentos y el documento).

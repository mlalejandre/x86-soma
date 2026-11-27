# Parche 349 · Fechas como Chrome, anchos mínimos y póster de vídeo

**Hace falta `make`.** Requiere el 348.

Diferencias con Chrome encontradas comparando webs reales (20minutos, PyPI). Todas son generales.

## 1 · Fechas y horas con las opciones de `toLocale…String` e `Intl.DateTimeFormat`

`toLocaleDateString`, `toLocaleTimeString` y `toLocaleString` (y con ellas `Intl.DateTimeFormat`, que se apoya en ellas) no hacían
caso de las opciones: salía siempre la fecha entera y la hora con segundos. Ahora siguen ECMA-402 en lo que usan las webs:

* solo los componentes pedidos: `weekday`, `year`, `month`, `day`, `hour`, `minute` y `second`;
* los formatos `numeric`, `2-digit`, `long` y `short`;
* `hour12` y `hourCycle`;
* en español, inglés de EE. UU., inglés británico y alemán (numérico).

| Llamada | Antes | Ahora (igual que Chrome) |
|---|---|---|
| `{day:'numeric', month:'long'}` | 26 de noviembre de 2026 | 26 de noviembre |
| `{hour:'2-digit', minute:'2-digit'}` | 22:15:07 | 22:15 |
| `{weekday:'short', day:'numeric', month:'short'}` | jueves, 26 de noviembre de 2026 | jue, 26 nov |
| `{day:'numeric', month:'long', hour:'2-digit', minute:'2-digit'}` | 26/11/2026, 22:15:07 | 26 de noviembre a las 22:15 |
| `en-US`, `{month:'short', day:'numeric'}` | Nov 26, 2026 | Nov 26 |
| `{hour:'2-digit', minute:'2-digit', hour12:true}` | 10:15:07 PM | 10:15 p. m. |

Hay 26 combinaciones comparadas con Chrome: todas iguales.

## 2 · Imágenes «comprimibles» en el ancho mínimo

Una imagen con `max-width` o `width` en porcentaje no aporta nada al ancho mínimo de su contenedor (CSS Sizing 3, §5.2.2), aunque
tenga un `width` fijo. SOMA ya lo tenía en cuenta, pero el `width` fijo lo deshacía: `img { width: 220px; max-width: 100% }` en una
celda de tabla se quedaba sus 220 px. En PyPI, la columna del logo medía 320 px en vez de 257 (Chrome) y la del texto se
estrechaba. Ahora las dos miden lo mismo que en Chrome, con 1 px de diferencia.

## 3 · Ancho mínimo con un tramo `nowrap`

El ancho mínimo (min-content) de un texto es el tramo más ancho entre dos puntos donde se puede cortar. Con un trozo `nowrap`, SOMA
tomaba el ancho de toda la línea desde el principio, también el texto normal de antes, que sí se podía cortar.

PyPI marca los enlaces externos con un icono, `a[target=_blank]:after { white-space: nowrap }`: cada párrafo con un enlace externo
se volvía inquebrantable. El párrafo «about» tenía 865 px de mínimo en vez de 122, la tabla se salía de su sitio y el texto no se
partía. Ahora el tramo empieza en el último corte posible: después de un espacio, de un carácter CJK y antes y después de un
elemento atómico.

## 4 · Póster de `<video>`

Un `<video poster="…">` se dibuja con su imagen de portada encajada en la caja (como `object-fit: contain`) y centrada, sobre el
fondo negro, como Chrome antes de reproducir. Antes solo salía un rectángulo negro con el triángulo. La portada se carga como
cualquier imagen visible.

## Pruebas

* `jsrun_host_274`: «fechas 349», con las 26 combinaciones y los resultados de Chrome.
* `test_web_bgimg_347`: póster de vídeo (centro con la imagen, bandas negras a los lados).
* `test_web_layout_181`:
  * imagen comprimible en una celda (fallaba con el código del 348: 270 px);
  * ancho mínimo con `nowrap` al final (fallaba: 381 px).

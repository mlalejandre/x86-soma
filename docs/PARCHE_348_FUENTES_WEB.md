# Parche 348 · Fuentes web (@font-face, WOFF y WOFF2) y Brotli

**Hace falta `make`.** Requiere el 347.

Hasta ahora SOMA no cargaba las fuentes de las páginas. Los titulares, menús y textos salían en la fuente de respaldo, y los
iconos de las fuentes de iconos (Font Awesome, Weather Icons, Material…) no salían. Casi todas las webs modernas sirven sus fuentes
en **WOFF2**, que va comprimido con **Brotli**, y SOMA no tenía Brotli. Este parche añade las dos cosas, solo en el navegador de
ring 3 (`websvc`).

## 1 · Brotli (`third_party/brotli`)

Es el decodificador de referencia de Google, licencia MIT (`third_party/brotli/LICENSE`), sin cambios. Para compilarlo sin libc
hay tres cabeceras mínimas en `third_party/brotli/soma/`: `malloc`/`free` van a `kmalloc`/`kfree`, y `memcpy`/`memset` son las del
propio servicio. Ocupa unos 170 KB (122 KB son el diccionario estático) y se compila sin SSE.

## 2 · De WOFF/WOFF2 a TrueType (`src/apps/web_wfont.c`)

`wff_to_sfnt` convierte una fuente descargada en un TrueType que dibuja el rasterizador de SOMA (`src/fonts/vfont.h`):

* **TrueType** (`.ttf`): tal cual.
* **WOFF 1.0**: cada tabla va comprimida con zlib.
* **WOFF 2.0**: todas las tablas van en un único flujo Brotli. Se reconstruyen:
  * `glyf`, que suele venir partida en 7 flujos (contornos, puntos, banderas, coordenadas en tripletes, compuestos, cajas e
    instrucciones);
  * `loca`, que viene vacía;
  * `hmtx`, cuando viene sin los `lsb`.

No se admiten las fuentes CFF (OpenType con contornos PostScript) ni las colecciones: dan error y la página usa la fuente de
respaldo.

Los datos vienen de Internet, así que:

* cada lectura comprueba el límite (leer fuera de sitio es un error, nunca un acceso);
* hay topes de tamaño: 4 MB descargados y 12 MB descomprimidos;
* hay topes de glifos, contornos, puntos y componentes.

Validación:

* Las fuentes reconstruidas son idénticas glifo a glifo a las de fontTools, la implementación de referencia: contornos, puntos,
  compuestos, `hmtx` y `cmap`. Se ha probado con Liberation Serif y Liberation Sans Bold (con y sin transformar `hmtx`) y con las
  fuentes variables reales de 20minutos (Source Serif y Roboto).
* 6000 archivos mutados al azar y 150 fuentes mutadas dibujadas a varios tamaños, bajo ASan y UBSan, sin ningún fallo de memoria.

En el rasterizador, la transformación de los glifos compuestos se hace en 64 bits, porque con datos hostiles podía desbordar un
`int32`.

## 3 · `@font-face` en el CSS

* **Lectura.** Se leen `font-family`, `src`, `font-weight` (también rangos, como `100 900`), `font-style` y `unicode-range`. De
  `src` se toma la primera `url()` en un formato que SOMA convierte (`woff2`, `woff`, `truetype`, `opentype`). Se saltan `local()`,
  `embedded-opentype` y `svg`.
* **`font-family`.** Toma la primera familia de la lista que sea de un `@font-face`, antes de cualquier familia conocida o
  genérica. La genérica de detrás (`serif`, `sans-serif`, `monospace`) sigue siendo la de respaldo.
* **Elección de la cara.** Se busca la que mejor casa con negrita y cursiva (CSS Fonts 4, simplificado): primero la cursiva,
  después el peso más cercano.
* **`unicode-range`.** Google Fonts parte cada familia por escrituras; se carga la que cubre el latín básico.

## 4 · Carga y dibujo

* **Descarga.** Las fuentes que usa algún elemento se descargan en el hilo de descargas, como las imágenes: como mucho 2 a la vez,
  16 por página. Al llegar, la página se vuelve a maquetar con ellas (como `font-display: swap`). Mientras tanto se ve la de
  respaldo.
* **Caché.** Las caras se guardan para toda la sesión del navegador (hasta 40 caras y 48 MB): la misma URL en otra página no se
  vuelve a descargar.
* **Iconos.** Con fuente web, los caracteres de la zona privada de Unicode (U+E000–U+F8FF) se dibujan si la fuente los tiene,
  también en `::before` y `::after`. Sin fuente web siguen sin ocupar sitio, como en el 345.
* **Respaldo por glifo.** Lo que la fuente web no tenga sale de la fuente de respaldo.
* **Negrita y fuentes variables.** De una fuente variable se dibuja la instancia por defecto. Si se pide negrita y la cara no lo
  es, se engorda el trazo sin cambiar el avance, como hace Chrome. Con Source Serif el ancho queda a un 1 % del peso 599 real, y el
  titular de 20minutos corta en las mismas palabras que en Chrome con la fuente web.
* **Fallos.** Una fuente que no se puede usar deja un aviso en el registro del navegador («fuente web: …»).

## 5 · Hojas de estilo diferidas

Muchas webs (LiteSpeed, WP Rocket, loadCSS) cargan el CSS no crítico así:

* `<link rel="preload" as="style" onload="this.onload=null;this.rel='stylesheet'">`, con una copia normal dentro de `<noscript>`;
* `<link rel="stylesheet" media="print" onload="this.media='all'">`.

SOMA no ejecuta el `onload` de un `<link>` y no dibuja `<noscript>`, así que esas hojas no se aplicaban nunca. En El Bierzo
Digital se perdían las fuentes de iconos (el icono del tiempo, el de WhatsApp) y parte del diseño. En Chrome acaban aplicadas,
así que SOMA las aplica desde el principio.

## 6 · `Content-Encoding: br`

Cuando no hay pasarela (el portátil, con TLS directo), el navegador pide `Accept-Encoding: gzip, deflate, br` y descomprime
Brotli si el servidor lo envía. Si el servidor no lo ofrece, sigue con gzip (20minutos, por ejemplo, solo ofrece gzip). Con la
pasarela no cambia nada, porque descomprime ella.

## 7 · `FontFace` en JavaScript

`new FontFace(familia, fuente, descriptores)` existe, con `load()`, `loaded` y `status`. `document.fonts` ya estaba (`ready`,
`check`, `load`).

## Pruebas

* `test_web_fuentes_348` (nueva) prueba la cadena entera en el anfitrión:
  * una fuente WOFF2 de 1 KB se convierte;
  * la hoja declara cuatro `@font-face` y se salta `eot`;
  * antes de cargar se usa la fuente de respaldo (458 px, como Chrome) y el icono no ocupa;
  * se piden la regular y la negrita, y de `unicode-range` la del latín;
  * con la fuente cargada el texto mide 417 px (Chrome: 417) y el icono U+F101, 28 px;
  * la misma URL reutiliza la cara;
  * `Content-Encoding: br` funciona bien, y falla con un flujo cortado y por encima del límite.
* `jsrun_host_274`: «fontface 348».

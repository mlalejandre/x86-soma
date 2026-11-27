# Parche 346 · El navegador, comparado con Chrome en webs reales

**Hace falta `make`.** Requiere el 345.

Todo lo de este parche salió comparando SOMA con Chrome en webs reales (20minutos.es y elbierzodigital.com), con la misma
página y el mismo ancho de ventana. Para cada diferencia se buscó la causa en el motor, se redujo a un caso mínimo y se comparó
con Chrome.

## 1 · `rem` se medía siempre sobre 16 px

`rem` vale el tamaño de letra de `<html>`, pero SOMA usaba siempre 16 px. Muchas webs ponen `html { font-size: 10px }` (o
`62.5%`) y miden en `rem`. En SOMA 20minutos salía un 60 % más grande y con otra maquetación: un titular de 22 px salía de 35.

Ahora se toma el tamaño calculado de `<html>` (`web_css.c`). Al propio `<html>` y a las consultas `@media` les corresponde el valor
inicial, como en el estándar.

## 2 · Clases sin prototype: «Cannot call a class as a function»

SOMA crea el `prototype` de una función la primera vez que se lee. Las operaciones que no lo leen no lo encontraban:

* `Object.defineProperty` y `Reflect.defineProperty`;
* `getOwnPropertyDescriptor` y `hasOwnProperty`;
* la lista de claves propias.

Babel hace `Object.defineProperty(Clase, "prototype", {writable: false})` en cada clase, y la clase se quedaba sin prototipo. Todo el
JavaScript de 20minutos fallaba nada más empezar. Ahora esas operaciones crean antes el prototipo pendiente (`js_fn_proto_ensure`).

## 3 · Eventos `load` y `error` de las imágenes

SOMA no disparaba nunca `load` ni `error` en ninguna imagen: ni en las de la página ni en las de `new Image()`. Fallaban:

* la detección de WebP de EWWW (muy común en WordPress);
* las precargas y galerías;
* las webs que tienen las fotos transparentes hasta su `onload`.

Ahora (`web_page.c`, `web_js_dom.c`):

* **Imágenes de la página.** Cuando una imagen llega o falla, cada `<img>` que la usa recibe su evento, una vez por `src`. Si el
  JavaScript cambia el `src`, se vuelve a disparar.
* **Imágenes del JavaScript.** Las `<img>` a las que el JavaScript pone `src` y que no están en la página se descargan igualmente y
  reciben su evento.
* **Propiedades.** `complete`, `naturalWidth` y `naturalHeight` dicen la verdad. Antes `complete` era siempre `true` y el tamaño
  natural era el de la caja. `img.width` de una imagen que no se dibuja es su tamaño natural.

## 4 · `srcset` con URLs `data:`

El `srcset` se cortaba en la primera coma. `srcset="data:image/gif;base64,R0lG..."` (WordPress) quedaba en `data:image/gif;base64` y
la imagen fallaba con «error de red». Ahora, como en el estándar, la URL llega hasta el primer espacio, y solo se le quitan las
comas finales.

## 5 · `window` y `document` con sus prototipos

`window` no heredaba de `Window.prototype`: una línea del preludio deshacía el enlace. `document` no heredaba de
`HTMLDocument.prototype`. Lo que una biblioteca añade ahí no les llegaba. La biblioteca Arrive, que usa EWWW, fallaba con
«arrive no es una funcion».

## 6 · Ancho intrínseco con flotantes

* En el ancho máximo del contenido, los flotantes de una misma fila se suman, como en Chrome; antes se tomaba solo el mayor.
* Los flotantes y los `inline-block` cuentan con su ancho fijo; antes solo con el de su contenido, y un `<li style="width:100px">`
  vacío medía 0.

El menú de El Bierzo Digital (`<li>` flotantes dentro de un elemento flex) medía casi nada y los puntos caían uno debajo de otro.

## 7 · Degradados con transparencia

* Un color con alfa 0 se pintaba opaco: `linear-gradient(transparent, #000)` era negro entero.
* La mezcla era canal a canal, no con alfa premultiplicado. La sombra sobre las fotos para leer el titular
  (`#fff0 → rgb(0 0 0 / .6)`) dejaba un velo blanco.

Ahora se interpola como en Chrome.

## Cómo se encontró

* **Capturas.** Chrome sin interfaz en el mismo entorno, a 1266 px como la ventana de SOMA, y capturas de las dos.
* **Sondas.** Una sonda de JavaScript lee los mismos valores calculados en los dos navegadores: tamaños, `display`, reglas que se
  aplican, antepasados.
* **Código real.** El JavaScript y el CSS reales de cada web, ejecutados en el anfitrión con el motor de SOMA, hasta reducir cada
  fallo a pocas líneas.
* **Maquetación diferencial.** Los mismos casos en SOMA y en Chrome, comparando las cajas.

## Pruebas

* `tests/test_web_rem_346.c` (nueva): `rem` con `html` en px, en % y en `rem`, en un hijo profundo, con `em`, con `calc()` y en
  `@media`.
* `tests/test_web_paint_346.c` (nueva): pinta y lee píxeles de degradados transparente→negro, `#fff0`→negro al 60 %, rojo
  transparente→rojo sobre azul y dos opacos. Valores de Chrome (±3).
* `tests/test_web_layout_181.c`, casos nuevos con valores de Chrome:
  * `<li>` flotantes en un elemento flex;
  * flotantes con `clear`;
  * un absoluto con flotantes;
  * una columna `auto` de rejilla con flotantes.
* `tests/jsrun_host_274/run.sh`, casos «prototype 346» y «window 346».
* En QEMU, comprobado:
  * `new Image()` con WebP y GIF en `data:`, una imagen remota y una que no existe, con `load`/`error`, `width`, `naturalWidth` y
    `complete` como en Chrome;
  * las portadas de 20minutos y El Bierzo Digital.

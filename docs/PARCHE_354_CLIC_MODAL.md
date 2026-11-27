# Parche 354 · Los clics llegan a los avisos de cookies y a los diálogos fijos

**Hace falta `make`.** Requiere el 353.

## El fallo

En Google, el aviso «Antes de ir a Google» se veía bien, pero ningún elemento respondía al clic: ni «Rechazar todo», ni «Aceptar
todo», ni «Más opciones», ni el enlace «Iniciar sesión». No se podía pasar del aviso. Pasa lo mismo con cualquier diálogo hecho
con `position: fixed` sobre una página que tenga elementos con `position: absolute` debajo, que son la mayoría de los avisos de
cookies, ventanas emergentes y menús.

## La causa (`src/apps/web_layout.c`, `hit_box`)

El dibujo y el clic no seguían el mismo orden:

* **Al pintar** la página: primero el flujo normal, luego los `position: absolute` por `z-index`, y por último, encima de todo, los
  `position: fixed`, ordenados por `z-index` y, a igual `z-index`, en orden del documento.
* **Al buscar qué hay bajo el ratón** se miraban antes los `absolute` y después los `fixed`, y estos sin tener en cuenta el
  `z-index`.

Por eso el clic sobre un botón del diálogo se lo quedaba lo que había debajo. Un fijo con `z-index` alto también perdía frente a
otro fijo de `z-index` más bajo que estuviera más adelante en el documento, como la capa gris de un aviso.

Ahora la búsqueda mira primero los fijos, en el orden inverso exacto al del pintado. Lo que se ve encima es lo que recibe el clic.
Afecta a todo lo que usa esa búsqueda: el clic, el puntero sobre los enlaces y `:hover`.

## Prueba nueva: `tests/test_web_clic_354.c` (en `make ci`)

Tiene 12 comprobaciones sobre una página como la de Google:

* contenido absoluto debajo, una capa gris fija y el diálogo fijo encima (botones, fondo del diálogo, la capa, con la página
  desplazada);
* dos fijos con `z-index` distinto y en orden contrario al del documento;
* dos fijos con el mismo `z-index`;
* un absoluto sobre el flujo, sin fijos, que se comporta como antes.

Con el código del 353 fallan 6, entre ellas las de los botones del diálogo; con este parche no falla ninguna.

No he podido abrir el aviso real de Google desde aquí: Google solo lo muestra a direcciones IP europeas y este entorno no tiene una.
Si después de este parche algún botón del aviso sigue sin responder, el siguiente sitio donde mirar es el JavaScript de Google
(que gestiona los clics con `jsaction`).

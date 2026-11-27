# Parche 353 · Caché HTTP del navegador y la «ventana fantasma»

**Hace falta `make`.** Requiere el 352.

## Por qué

En el portátil, SOMA habla TLS directamente con cada sitio (parche 176A) y el navegador no tenía ninguna caché. Cada página
volvía a bajar todo su CSS, JavaScript, fuentes e imágenes, también al volver atrás o al abrir otra noticia del mismo sitio. En
QEMU no se notaba tanto porque la pasarela del anfitrión tiene su propia caché, pero incluso así cada recurso cruzaba de nuevo la
red virtual y se volvía a descomprimir.

Los sitios declaran cuánto se puede guardar cada recurso. Ejemplos de 20minutos:

| recurso | cabeceras |
|---|---|
| `global.bundle.js` | `cache-control: public, max-age=31536000, immutable` |
| `loader.js` del consentimiento | `cache-control: max-age=7200, public` |
| la portada (HTML) | `max-age=0, s-maxage=15` y `set-cookie`: no se guarda |

## Qué hace

**Qué se guarda** (`wn_cache_secs`, en `web_net.c`, a partir del bloque de cabeceras de la respuesta). Se guarda una respuesta GET
con estado 200, sin redirecciones, sin cuerpo y sin cabeceras propias, si cumple todo esto:

* trae `Cache-Control: max-age=N` mayor que su `Age`;
* no trae `no-store` ni `no-cache` (SOMA no revalida, así que `no-cache` cuenta como `no-store`);
* no trae `Set-Cookie`;
* no trae `Vary` distinto de `Accept-Encoding`. Una imagen negociada con `Vary: Accept` no se guarda.

`s-maxage` es para cachés compartidas y no cuenta. Las respuestas de la pasarela (`no-store`) tampoco se guardan, porque la
pasarela ya tiene su caché.

**Dónde se guarda** (`src/apps/web_hcache.c`, solo en el proceso aislado del navegador):

* En memoria, nunca en el disco.
* Hasta 12 MiB, o 48 MiB si el navegador tiene 384 MiB o más de montón (parche 341).
* Hasta 512 entradas. Una sola respuesta no puede pasar de 4 MiB ni de un cuarto del tope.
* Al llenarse sale la menos usada.
* La clave es la URL pedida y si se pidió como binario.
* Se sirve una copia mientras sigue fresca (`max-age − Age`, como mucho 7 días), sin preguntar al servidor. Caducada, se borra y se
  pide otra vez.

**Quién la usa:** las hojas de estilo (en síncrono y en paralelo, parche 352), los scripts (también los adelantados), las fuentes
web, las imágenes y `fetch`/`XMLHttpRequest` GET de la página. **No la usa la navegación principal:** el HTML de la página se pide
siempre al servidor, como al recargar en Chrome.

## Medido en QEMU

Hay dos páginas en un servidor local (sin pasarela). Las dos comparten dos hojas de estilo, un script y una imagen, servidos con
`Cache-Control: max-age=600`, más una hoja con `no-cache`. Se abre la primera, luego la segunda y después otra vez la primera.
Peticiones que llegan al servidor:

| | antes | con el 353 |
|---|---|---|
| página 1 | html, 3 hojas, script, imagen | html, 3 hojas, script, imagen |
| página 2 | lo mismo otra vez | html y la hoja `no-cache` |
| página 1 otra vez | lo mismo otra vez | html y la hoja `no-cache` |

Las páginas que salen de la caché se ven igual: estilos, script ejecutado e imagen.

## Seguridad

* No se guardan respuestas que fijan cookies.
* Nada sale del proceso aislado del navegador, ni al disco ni a otros procesos.
* Al cerrar el navegador la caché desaparece con el proceso.

## La «ventana fantasma» y el borde izquierdo cortado (`src/ui/gui.c`)

Al abrir el navegador con la orden `navegador <dirección>` (al arrancar, desde el terminal o el agente) aparecía arriba a la
izquierda una copia recortada de la ventana del navegador, sin barra de título. Quedaba ahí mientras la página se movía (carga,
GIF animados, cursor de texto) y se iba sola después. Además, esa copia montaba 14 px sobre la ventana de verdad y le cortaba el
borde izquierdo: «CSS» salía como «SS», «script» como «cript». Eran los dos fallos pendientes de «ventana fantasma al arrancar»
y «borde izquierdo cortado».

La causa:

* Esa orden abre la ventana desde otro hilo con `her_open_app`, que actualiza la geometría de la ventana activa (`her_win_*`).
* El bucle de la GUI trabaja con sus propias copias (`win_x`, `win_y`, `win_w`, `win_h`), y estas solo se ponen al día cuando el
  propio bucle llama a `HER_SYNC_WIN()`.
* Hasta entonces, el fotograma rápido que repinta solo el navegador (parche 152) lo dibujaba con la geometría de la primera ventana
  del escritorio. Con esas mismas copias se repartía el ratón del navegador.

Ahora el bucle las pone al día en cada vuelta: son cuatro asignaciones. Comprobado en QEMU con la misma orden: ya no hay copia y
el borde izquierdo se ve entero.

## Prueba nueva: `tests/test_web_cache_353.c` (en `make ci`)

Tiene 38 comprobaciones:

* qué se guarda, con bloques de cabeceras reales (los de 20minutos de la tabla) y con cada caso que no se guarda;
* que se sirve una copia propia con el mismo tipo, juego de caracteres y URL final;
* la clave binario/texto y el límite de bytes de quien pide;
* la frescura (sirve a los 599 s de un `max-age=600`, no a los 601);
* el tope de memoria, sacando la menos usada;
* que una respuesta sustituye a la anterior, y vaciar la caché.

Pasa con AddressSanitizer y sin fugas.

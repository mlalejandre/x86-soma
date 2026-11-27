# Parche 345 · El navegador, probado en webs reales

**Hace falta `make`.** Requiere el 344.

Hasta ahora el navegador se probaba con páginas hechas a medida. Este parche sale de usarlo con webs reales (El País en el
ThinkPad; PyPI, yarnpkg.com, npm y crates.io en QEMU) y de arreglar lo que fallaba en ellas.

## 1 · Imágenes que fallaban con «no se pudo conectar»

Cada proceso de ring 3 tiene un número fijo de sockets: 4. Desde el parche 343 el navegador quería más: 4 descargas a la vez, la
petición principal y hasta 4 conexiones guardadas abiertas. En una web que sirve desde varios servidores (El País:
`elpais.com`, `static.elpais.com`, `imagenes.elpais.com`) el núcleo respondía «ocupado» y la imagen fallaba con «no se pudo
conectar». Con un solo servidor no pasaba, por eso no salió en las pruebas del 343.

* **8 sockets para el navegador** (`usermode.c`, `web_r3.c`). El límite pasa a ser por proceso (`uexec_spec.max_sockets`): el
  navegador tiene 8 (`USER_MAX_SOCKETS_WEB`); los demás servicios siguen con 4.
* **El núcleo admite 24 conexiones TCP** (antes 16, `tcp.c`), para que el navegador no deje sin sitio al correo, YouTube o `netsvc`.
* **Las conexiones guardadas ceden el sitio** (`web_net.c`). Si no se puede abrir una conexión nueva y hay alguna guardada sin usar,
  se cierra la más antigua y se intenta otra vez.

Página de prueba con 60 fotos repartidas en 4 servidores: antes, 33 fallaban en la ventana; ahora, ninguna.

## 2 · La pasarela de QEMU ya no se da por caída con un fallo suelto

Un solo fallo al conectar con la pasarela (puerto 8096) la marcaba como ausente 20 s y todo iba al proxy antiguo (8095), que casi
nunca está: fallaban todas las imágenes seguidas. Ahora solo se da por ausente si nunca ha respondido; si ya funcionaba, el fallo
es solo de esa petición.

## 3 · Imágenes: reintento con espera

Tras un fallo de red la imagen se repetía al momento, una vez. Ahora se repite dos veces, con espera (0,4 s y 1,5 s), para
fallos pasajeros. Un 404 o un error del servidor no se repiten. En la barra de estado ya no sale el «0» suelto de un fallo de
conexión («no se pudo conectar 0»).

## 4 · `<noscript>` nunca se dibuja

El reto anti-bots de PyPI (Fastly) pone `noscript { display: flex; height: 100vh }`. SOMA lo respetaba y pintaba el HTML del
`<noscript>` en crudo a pantalla completa, con el mensaje de verdad debajo, fuera de la vista. Chrome y Firefox no lo dibujan nunca
con JavaScript, diga lo que diga el CSS. Ahora SOMA tampoco (`web_css.c`). Las webs de noticias meten ahí los píxeles de seguimiento.

## 5 · Iconos «?»

PyPI, como gran parte de la web, usa una fuente de iconos (Font Awesome). Sus dibujos están en la zona de uso privado de Unicode
(U+E000–U+F8FF), y SOMA, que no carga fuentes web, dibujaba un «?» en cada pestaña, botón y enlace externo. Sin la fuente, esos
caracteres no ocupan sitio ni se dibujan (`web_font.c`). Cargar las fuentes de verdad queda pendiente.

## 6 · MutationObserver: de 64 a 4096

Stimulus (PyPI, GitHub, las webs hechas con Rails) crea varios observadores por componente; la ficha de un paquete en PyPI pasaba
de 500 y el 65 daba «RangeError: Demasiados observadores». Además, cada cambio del DOM recorría todos los observadores. Ahora hay
un índice por nodo observado: cada cambio mira solo el nodo y sus antepasados. Límite: 4096 observadores y 1024 nodos por
observador. Un observador que observa un nodo dos veces, o un nodo y su antepasado, sigue dando un solo registro por cambio.

## 7 · JavaScript moderno que faltaba

`yarnpkg.com` se rompía entera con «__getOwnPropDescs no es una funcion»: esbuild y Vite traducen `{...a, b}` a
`Object.getOwnPropertyDescriptors`, que no existía. Añadidos (`web_js_prelude.h`):

* `Object.getOwnPropertyDescriptors`, `Object.groupBy`, `Map.groupBy`;
* `Array.prototype.toSpliced`, `Array.fromAsync`;
* `String.prototype.isWellFormed` y `toWellFormed`;
* los métodos nuevos de `Set` (`union`, `intersection`, `difference`, `symmetricDifference`, `isSubsetOf`, `isSupersetOf`,
  `isDisjointFrom`);
* `AbortSignal.any`;
* `__defineGetter__`, `__defineSetter__`, `__lookupGetter__` y `__lookupSetter__`.

Y los **símbolos** (`web_js_lib.c`, `web_js_run.c`):

* `Reflect.ownKeys` no devolvía ninguno;
* `Object.getOwnPropertySymbols` devolvía las claves de texto;
* `Object.assign`, `Object.defineProperties` y `{...o}` no copiaban los símbolos.

Las ranuras internas del motor (`@@st`, `@@cl`, `@@ds`…) siguen sin verse.

## Pruebas

* `tests/test_web_mutation_147.c`, ahora en `make test` (no estaba conectada), con casos nuevos:
  * 600 observadores;
  * observar dos veces el mismo nodo;
  * un nodo y su antepasado;
  * desconectar y volver a observar.
* `tests/jsrun_host_274/run.sh`, caso «es2024 345»: todo lo añadido de JavaScript y los símbolos (también que `@@st` no se ve).
* En QEMU, con webs reales a través de la pasarela:
  * PyPI (portada, ficha de un paquete, búsqueda, inicio de sesión con el control «Mostrar contraseña»);
  * yarnpkg.com;
  * la portada de prueba de 4 servidores.

## Visto y pendiente

* **Cloudflare** (npm y muchas otras): «Navegador no compatible». SOMA no tiene Web Workers, WebAssembly, `crypto.subtle` ni
  iframes.
* **yarnpkg.com:**
  * la animación de la portada necesita WebGL;
  * un script supera los 12 s de ejecución en QEMU sin KVM (hay que medirlo en hardware real);
  * el error señala un archivo equivocado;
  * la barra de navegación sale blanca y el logotipo, blanco, casi no se ve.
* **Páginas de más de 8 MB:** se cortan sin avisar.
* **Errores HTTP con el cuerpo vacío:** sale una página en blanco, en vez de una página de error.
* **Iframes:** no se dibujan.
* **Ventana:** aparece una ventana fantasma al abrir el navegador.
* **Texto:** se recorta en el borde izquierdo.

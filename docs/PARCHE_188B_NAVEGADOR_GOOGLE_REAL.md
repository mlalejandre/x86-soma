# Parche 188B — Navegador: Google real (aviso de cookies, caja de búsqueda) y motor JavaScript

Requiere el parche 188A. Es el primer parche del navegador depurado contra las páginas **reales** de Google y YouTube
(descargadas con la identidad del navegador de SOMA y ejecutadas en el motor), no contra réplicas. Las réplicas de los
parches 188 y 188A funcionaban; lo que fallaba estaba en otra parte.

## Qué fallaba de verdad

1. **Memoria de JavaScript.** El intérprete no tiene recolector de basura y analizar un script cuesta unos 64 bytes de
   montón por byte de fuente. Con menos de 2 GiB de montón el tope era de 24 MiB fijos (QEMU arrancaba con 512 MB): el
   primer script grande de Google lo agotaba, el intérprete se **detenía para toda la página** y los botones
   Aceptar/Rechazar —que ya tenían su manejador puesto— dejaban de responder. Ahora:
   - el tope es 1/4 del montón del núcleo, entre 24 y 1536 MiB (T480s: 1536 MiB; antes 512);
   - un script que no cabe se rechaza **antes** de analizarlo (aviso en `about:log`) en vez de gastar la memoria;
   - la primera vez que se agota la memoria solo falla el script en curso y queda una reserva de 1/4 para los
     manejadores ya instalados; el intérprete solo se detiene si se agota también la reserva;
   - `ejecutar.py` arranca la sesión interactiva de QEMU con 4 GiB (`SOMA_MEM` lo cambia; la prueba de arranque sigue
     con 512 MB).
   Comprobado con la página real: el aviso se cierra con cualquier tope, de 24 MiB en adelante.
2. **URL de más de 1024 caracteres.** El script principal de Google tiene una URL de casi 4000 caracteres; se truncaba
   y Google respondía 404. Límite nuevo: 8192 (historial y barra de direcciones: 2048). Para no desbordar la pila de
   los hilos del núcleo (60 KiB), la resolución de URL usa un búfer del montón, y `wn_fetch` y los hilos de descarga
   corren en una pila propia de 256 KiB. El servicio aislado `netsvc` conserva 1024.
3. **`this` en funciones no estrictas.** `(function(){ var window = this; ... })()` es como empiezan los scripts de
   Google; `this` valía `undefined` en vez del objeto global y todo el fichero fallaba. El analizador distingue ahora
   código estricto (`'use strict'`, clases) del que no lo es (ES 10.2.1.2). Interruptor: `soma.jssloppy=off`.
4. **`BigInt`.** Google lo usa al cargar (`BigInt(Number.MAX_SAFE_INTEGER)`). Nuevo `src/apps/web_js_big.c`: enteros de
   128 bits con signo, literales `123n`, operadores, comparación con números, `asUintN`/`asIntN`, `toString(base)`,
   `typeof` "bigint". Interruptor: `soma.jsbigint=off`.
5. **Expresiones regulares con más de 63 grupos** (Google tiene una con cientos): límite 255.

## Lo que salió al ejecutar YouTube real

- **Huecos de array**: `Array(3).concat(a, b).map(f)` llamaba a `f` con `undefined` en los huecos (D3, dentro de
  YouTube). `Array(n)`, `[,,]`, `delete a[i]` y `a.length = n` crean huecos; `forEach`/`map`/`filter`/`some`/`every`/
  `reduce`/`indexOf`/`in`/`Object.keys` los saltan; `[...a]`, `Array.from` y `Array.apply` los convierten en `undefined`.
- **`Function.prototype.toString`** devolvía el texto sin `function` y con restos del código siguiente (el inyector de
  dependencias de YouTube lo analiza). Ahora devuelve el texto exacto, también para métodos y clases.
- DOM: clases `Window` (con `window instanceof Window`), `CDATASection`, `ProcessingInstruction`; `children`,
  `firstElementChild`… en `DocumentFragment` y `Document`; `element.attributes` como `NamedNodeMap`; un método llamado
  `async`/`title`/… en un prototipo que hereda de `HTMLElement` (Polymer) ya no tropieza con el atributo reflejado;
  más de cien interfaces que un navegador siempre tiene (`Range`, `DOMRect`, `Storage`, `Navigator`, `CSSRule`,
  `HTML…Element`, `PromiseRejectionEvent`, `AggregateError`…).
- `about:log` dice de qué script viene cada error no capturado.

## Límites conocidos (medidos, no supuestos)

- **YouTube sigue mostrando solo el esqueleto.** Su aplicación son 10,8 MB de JavaScript: el motor necesita más de
  3 GiB de montón para arrancarla porque no libera memoria (no hay recolector) y el árbol sintáctico es voluminoso.
  Con este parche el script ya carga y se ejecuta hasta construir `ytd-app`; lo que falta es un recolector de basura
  y un árbol más compacto, que es trabajo para un parche propio.
- **Resultados de búsqueda de Google.** La portada, el aviso y la caja de búsqueda funcionan con la página real. Al
  buscar, Google responde con un desafío en JavaScript contra programas automáticos; el motor lo ejecuta, calcula la
  cookie `SG_SS` y recarga, pero en la prueba (hecha con el reloj simulado del banco de pruebas y `curl`) Google no
  aceptó el resultado y contestó "tráfico inusual". No se ha podido comprobar en el equipo real.
- La portada de Google gasta 260 MiB de JavaScript. Con poca memoria se rechaza su script principal: el aviso y la
  búsqueda por formulario funcionan, las sugerencias no.

## Verificación

- `tests/test_web_js_188.c`: 151 casos (56 nuevos) con ASan.
- Página real de Google en el motor y en QEMU (512 MB, 2 GiB y 6 GiB): rechazar cookies, escribir y enviar.
- 160 páginas del corpus: mismas métricas que antes, ninguna peor, dos con menos errores; con ASan, sin errores.
  ASan también sobre Google y YouTube reales.
- Núcleo x86 y ARM con `WERROR=1`; `__kernel_image_end` dentro del límite.

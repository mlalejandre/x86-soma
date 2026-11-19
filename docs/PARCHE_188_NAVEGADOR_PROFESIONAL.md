# Parche 188 — Navegador profesional

Requiere el parche 187A. Es el parche grande del navegador: se ha comparado SOMA con Chrome (Chromium 153) página a
página sobre 160 páginas reales guardadas (las 130 de pruebas de Mozilla Readability y las aplicaciones de WebKit
Speedometer: TodoMVC en React, Redux, Preact, Vue, Angular, Svelte, Lit, Backbone, jQuery, web components, Next.js,
Nuxt...), con la misma red (copia local), las mismas fuentes y el mismo tamaño de ventana, y se han corregido las
diferencias de mayor impacto.

## Por qué no se podía escribir en la caja de Google (sin cursor, "como si no hiciese clic")
La caja de búsqueda de Google es un `<textarea>` dentro de una fila flex que empieza con un `::before`. SOMA no
generaba `::before`/`::after`, así que la fila se maquetaba distinta: el textarea quedaba 37 píxeles por encima de
donde se ve la barra (invisible) y el clic caía fuera. Ahora:

- `::before` y `::after` reales (contenido, `attr()`, contadores, comillas, *clearfix*), también en flex y grid.
- `pointer-events: none` (las capas decorativas ya no se tragan el clic).
- Un clic dentro de la barra que no cae exactamente en el campo enfoca el campo de texto de esa barra.
- `autofocus` también en campos que aparecen después de cargar (Google lo hace con JavaScript).
- El cursor se ve en cuanto haces clic o escribes, y parpadea como en Chrome (antes empezaba apagado y, al escribir,
  podía quedarse oculto hasta el siguiente parpadeo).

Probado con una réplica de la barra de Google: clic, cursor parpadeando, escritura y Enter igual que en Chrome.

## Novedades

### JavaScript
- **Proxy** completo (todas las trampas, `Proxy.revocable`) y **Reflect** ampliado (`Reflect.set` con receptor como
  manda el estándar: arregla Vue 3/Nuxt, que entraban en recursión infinita, y core-js, que contaminaba
  `Object.prototype` y rompía Next.js).
- **Módulos ES** (`<script type=module>`, `import`/`export` en todas sus formas, `import()` dinámico, `import.meta`,
  *import maps*, `nomodule`).
- **eval directo** (ve y modifica las variables de la función que lo llama; antes era siempre global).
- **Plantillas etiquetadas** con objeto de cadenas único por punto del código y congelado (lit-html cachea por él;
  antes cada repintado rehacía el DOM y se perdía el foco).
- `with`, bloques `static {}` de clase (Lit los usa para sus estilos), expresiones de función con nombre,
  escapes Unicode en identificadores.
- `Error.stack` con la pila real de llamadas (`at funcion (script:linea)`), útil en `about:log`.
- Más pila: las funciones `async` tienen pila de 1 MiB y se admiten 800 llamadas anidadas (con >= 2 GiB de montón).
- El montón de cada página crece hasta 1/6 del montón del núcleo (128-512 MiB): Nuxt necesitaba ~250 MiB.

### DOM y Web Components
- **Shadow DOM** real: `attachShadow` (open/closed), `<slot>` con nombre y por defecto, CSS acotado (`:host`,
  `:host()`, `:host-context()`, `::slotted()`), herencia a través del árbol compuesto.
- **Custom elements**: `customElements.define/get/getName/whenDefined/upgrade`, constructor `HTMLElement` válido para
  `super()`, `connectedCallback`/`disconnectedCallback` síncronos, `attributeChangedCallback` con `observedAttributes`.
- **CSSStyleSheet construible** (`new CSSStyleSheet`, `replace`, `replaceSync`, `insertRule`...) y
  `adoptedStyleSheets` (en documento y raíces sombra; también `push`).
- `<template>.content`, `getRootNode`, `append`/`prepend`/`replaceChildren` en fragmentos.
- **Comentarios en el árbol** (lit-html, Vue y otros los usan como anclas) y `TreeWalker`/`NodeIterator` vivos según
  el estándar (antes eran una lista precalculada: lit-html no encontraba sus marcadores).
- Analizador HTML: un `<head>` tardío (después de contenido) ya no se traga la página (Yahoo salía en blanco); `<head>`
  implícito antes del cuerpo.
- **`innerText`** según la maquetación (sin texto oculto ni de `<script>`, saltos por bloques, tabuladores en tablas,
  `text-transform`): coincide con Chrome en 156 de 160 páginas (antes 61).
- **`getComputedStyle`** con muchas más propiedades (flex, posiciones, `transform`, `box-shadow`, bordes, `gap`...)
  y **variables CSS** (`getPropertyValue('--color')`).
- **Eventos a través del shadow DOM** como en el estándar: los eventos `composed` (todos los de usuario: clic,
  teclado, foco...) salen del árbol sombra hasta el documento, pasan por los `<slot>`, el `target` se reajusta al
  anfitrión para quien escucha desde fuera y `composedPath()` oculta los árboles cerrados. Sin esto, en Lit o en
  cualquier web component, pulsar Enter en un campo no llegaba a la aplicación.
- Los custom elements creados por `importNode`, `cloneNode`, `innerHTML` o `insertAdjacentHTML` se mejoran en el acto
  (como Chrome); `text` solo existe en `<a>`, `<script>`, `<option>` y `<title>` (en otros escribía en el DOM).
- Comentarios "falsos" `<?...>` y `<!...>` del analizador HTML (lit-html marca así sus huecos de texto).
- Si el elemento enfocado desaparece del documento, nadie tiene el foco (antes se seguía escribiendo en un nodo
  invisible).
- `on*` (onclick...) en los prototipos (React los detecta así), `DOMParser` y documentos separados (jQuery 3 ya no
  borra el `<body>`), `document.title` y título de la pestaña con espacios colapsados.

### CSS y maquetación
- Pseudo-elementos `::before`/`::after` y `::placeholder` (color, cursiva, negrita).
- Controles de formulario con las medidas de Chrome: `<input>` (relleno 1px 2px, borde de 2px dibujado como el tema,
  ancho según `size` y la fuente), `<textarea>`, `<select>`, botones; `box-sizing: border-box` también en el alto.
- `grid-auto-flow: column`, `place-content`, `inset-inline`/`inset-block`, radios y bordes lógicos, `grid-row-gap`/
  `grid-column-gap`; `margin-inline: a b` asignaba mal los dos valores.
- **Imágenes de fondo CSS**: se pedían solo al pintar y el cargador las daba por lejanas, así que no se cargaban
  nunca (iconos, círculos de TodoMVC, decoraciones...). Ahora se cargan en cuanto están a la vista.
- Hasta 5 sombras exteriores en `box-shadow` (efecto de hojas apiladas) y `box-shadow: inset`; `font-size: 0`
  oculta el texto.
- `transform: rotate()/scale()` en cajas decorativas,
  `text-overflow: ellipsis` y `-webkit-line-clamp`, `clip`, `content`, contadores, `fill`/`stroke` para SVG en línea.
- **Error grave corregido:** un `<![CDATA[` dentro de un `<style>` hacía que se saltase CSS hasta el siguiente `>` y
  aparecían reglas universales (en MathJax todo el `<head>` salía como texto). Ahora solo se ignoran `<!--` y `-->`,
  como en el estándar.
- Variables CSS encadenadas hasta 20 niveles (los sistemas de diseño como Spectrum usan cadenas largas).
- SVG con solo `viewBox` ocupa el ancho disponible; los SVG se vuelven a rasterizar si se muestran más grandes.

## Resultados frente a Chrome (160 páginas, misma red y fuentes)

| Métrica | 187A | 188 |
|---|---|---|
| Cajas que coinciden con Chrome (media) | 0,43 | 0,70 |
| Diferencia media de píxeles | 4,8 | 2,2 |
| Páginas con >= 90 % de cajas iguales | 22 | 49 |
| Páginas con diferencia de píxeles < 1 | 45 | 66 |
| `innerText` igual (±10 %) | 61 | 156 |
| Títulos iguales | 156 | 158 |
| Páginas con errores de JavaScript | 87 | 78 |

TodoMVC interactivo (escribir dos tareas con Enter): React, Redux, Preact, Vue, Svelte, Backbone, jQuery, vanilla
igual que Chrome; Lit y web components ahora se dibujan, responden y añaden las tareas como Chrome (antes salían
sin estilos o vacíos). Nuxt pasa de
página en blanco a 0,74 de cajas y 1,6 de diferencia de píxeles.

## Interruptores (`soma.X=off` en la línea de arranque)
| Interruptor | Desactiva |
|---|---|
| `soma.webcomp=off` | shadow DOM y custom elements (vuelve al apaño anterior) |
| `soma.jsmodule=off` | módulos ES (`type=module` se ignora y `nomodule` se ejecuta) |
| `soma.jsproxy=off` | Proxy |
| `soma.jsdeval=off` | eval directo (vuelve a ser global) |
| `soma.htmlparse=off` | comentarios en el árbol y arreglos de `<head>` |
| `soma.innertext=off` | `innerText` por maquetación (vuelve a ser `textContent`) |
| `soma.weblayout=off` | maquetación nueva (incluye la de este parche: controles, `border-box` en alto, grid...) |

## Verificación
- `tests/test_web_js_188.c`: 92 casos (Proxy, Reflect, with, static, eval directo, Error.stack, pila async, on*,
  arrays genéricos, plantillas etiquetadas, eventos por shadow DOM, comentarios, TreeWalker, NodeIterator, shadow DOM, custom elements, CSSStyleSheet, template) con
  ASan/UBSan. Las pruebas de 146 a 187A pasan sin cambios.
- Motor completo en el anfitrión con ASan sobre las 160 páginas y sobre las pruebas interactivas (Google, Lit, web
  components, React, Vue, Nuxt), también con la memoria nueva rellenada con basura como en el núcleo (0xDD): sin
  errores. Así se encontraron y corrigieron un uso tras liberar (cajas de nodos sombra tras rehacer la maquetación),
  una lectura fuera de límites en el analizador y un **pánico del núcleo** (#GP en `compute_node`: punteros de
  `::before` sin inicializar) que en QEMU aparecía al abrir la TodoMVC de Lit.
- QEMU (núcleo real, 6 GiB): réplica de Google (clic, cursor parpadeando, escritura), web components, TodoMVC de
  Lit (clic y dos tareas con Enter), React, Nuxt, Next.js y MathJax sin pánicos.
- Núcleo x86 (T480s) y ARM con `WERROR=1`; imagen del núcleo por debajo de 14 MiB.

## Límites conocidos
- `document.write` durante el análisis no se ejecuta en su sitio (los scripts se ejecutan tras analizar): páginas
  antiguas que escriben HTML desde el `<head>` pueden quedar distintas.
- No se distingue el modo estricto (`'use strict'`) en `eval`.
- `BigInt` no existe todavía; `@font-face` (fuentes web descargadas) tampoco.
- Las fuentes de tamaño fraccionario se redondean (13,333 px se dibuja a 13 px): campos sin estilo ~4 % más estrechos.

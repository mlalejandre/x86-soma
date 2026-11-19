# Parche 188A — Navegador: diálogos, iconos SVG, imágenes y Polymer

Requiere el parche 188. Corrige lo que se vio en el T480s tras el 188: los botones Aceptar/Rechazar del aviso de
Google no hacían nada, muchos iconos fallaban con "formato desconocido", 20minutos.es se quedaba "sin memoria para
más imágenes" y YouTube solo mostraba la estructura.

## Qué fallaba y qué se ha hecho

1. **`transitionend` y `animationend` no existían.** SOMA no anima, pero las páginas esperan esos eventos para
   continuar: el diálogo de cookies se desvanece con una transición y se retira al recibir `transitionend`; si no
   llega, el clic "no hace nada". Ahora se interpretan `transition*` y `animation*` (duración, retardo, repeticiones,
   propiedades) y, cuando cambia el estilo de un elemento con transición, se emite `transitionend` pasada su
   duración (`propertyName`, `elapsedTime`); las animaciones emiten `animationstart` y `animationend`
   (`animationName`). Comprobado frente a Chrome: mismos eventos y en el mismo orden.
2. **Iconos SVG en `data:` con escapes CSS** (`url("data:image/svg+xml,\00003csvg ...")`, usados por Google y
   YouTube): el escape `\00003c` es `<` y no se decodificaba. Ahora se decodifican los escapes CSS en `url()`.
3. **Memoria de imágenes**: 20 MiB por página no bastaban para un portal de noticias. Con memoria grande (>= 2 GiB de
   montón, el T480s) son 192 MiB; las imágenes lejanas se siguen expulsando como antes.
4. **Polymer (YouTube)**: los nodos de una `<template>` pertenecen ahora a un documento inerte, como en Chrome (los
   custom elements de su interior no se activan antes de tiempo) y la mejora de componentes ignora el contenido de
   las plantillas. Probado con una aplicación Polymer 3 al estilo de YouTube (elementos `Polymer({is})`,
   `dom-repeat`, `dom-if`, datos de `ytInitialData`, eventos `on-tap`): igual que Chrome.
5. **Selector `:has()`** (descendiente, `>`, `+`, `~`): antes la regla entera se descartaba.
6. `new FormData(form, botón)` incluye el botón que envía; propiedad `inert` (y `[inert]` no recibe clics).

## Interruptores
Todo lo de maquetación cae bajo `soma.weblayout=off`; la parte de componentes, bajo `soma.webcomp=off`.

## Verificación
- `tests/test_web_js_188.c`: 95 casos (3 nuevos), con ASan; pruebas 146–188 sin cambios.
- Motor en el anfitrión con ASan sobre 160 páginas reales, sobre las réplicas del aviso de Google, de transiciones,
  de Polymer y sobre las TodoMVC interactivas: sin errores. TodoMVC: las 14 igual que Chrome.
- Núcleo x86 y ARM con `WERROR=1`; QEMU sin pánicos.

## Límites
- No he podido abrir google.com ni youtube.com desde el entorno de desarrollo: las pruebas usan réplicas. Si el
  aviso sigue sin cerrarse, `about:log` mostrará el error de JavaScript que lo impide.
- No hay animación visual: el estado final se muestra al momento y los eventos llegan a su tiempo.

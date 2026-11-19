# Parche 187A — Navegador: arreglos para poder buscar en google.com

Requiere el parche 187. Sin interruptor propio (son correcciones de comportamiento estándar).

## Qué fallaba
Captura de google.com en el T480s: el cuadro de búsqueda mostraba "Choose file / No file chosen" y un botón vacío, y no se podía buscar.

1. **`<input type=file hidden>` se dibujaba.** El atributo `hidden` se ignoraba precisamente en los `<input>` (en `div` sí funcionaba). Google lleva un input de archivo oculto para el menú "+" (subir imagen o archivo); al pintarse ocupaba sitio dentro de la barra y empujaba el campo de texto.
2. **`keypress` no se lanzaba con Enter** y, en las teclas normales, se lanzaba *después* de insertar el carácter (no se podía cancelar). Ahora va entre `keydown` y la acción por defecto, como en los navegadores.
3. **`document.f` y `form.q`** (acceso por nombre a formularios y a sus campos) devolvían `undefined`; muchos scripts de búsqueda los usan.
4. **`KeyboardEvent.code`** salía como `Key` o `Digit` sin la tecla; ahora `KeyH`, `Digit7`, `Space`, `Minus`, `Enter`...
5. **`InputEvent`** ahora trae `inputType` (`insertText`, `insertLineBreak`, `deleteContentBackward`).
6. **CSS "visualmente oculto"**: `clip: rect(0,0,0,0)` y `clip-path: inset(50%)` ocultan el elemento.

## Novedad: `about:log`
Escribe `about:log` en la barra de direcciones: muestra los últimos 48 avisos y errores de JavaScript y del navegador (por ejemplo `Uncaught ReferenceError: ...`). Si una página sigue sin funcionar, ese registro dice por qué.

## Verificación
- `tests/test_web_js_187a.c`: 24 casos (acceso por nombre, expandos del documento, InputEvent), con ASan/UBSan.
- QEMU: página con la barra de Google replicada: se escribe, Enter envía `GET /search?q=hola`; el input oculto ya no se ve; `about:log` lista errores provocados.
- Pruebas de 181, 182, 183 a 187 sin cambios.

## Límites conocidos
- Un `<input type=file>` con `position:absolute` dentro de un flex anidado todavía puede reservar ancho (visto solo en una réplica de prueba).
- `transform: scale(0)` y `font-size: 0` en controles no los ocultan.
- No he podido descargar google.com desde el entorno de desarrollo; las pruebas usan una réplica de su barra. Si tras este parche sigue sin buscar, `about:log` es lo primero que hay que mirar.

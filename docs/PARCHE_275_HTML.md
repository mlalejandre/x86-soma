# Parche 275 · HTML y JavaScript en el Programador

Requiere el 274 (js). Añade la orden `html` y mete **JavaScript** y **HTML** como lenguajes del Programador, junto a SOMA-LANG, Python y C.

## La orden `html`
- `html nuevo NOMBRE [basica|app|juego]` crea `/workspace/NOMBRE/` con `index.html`, `estilo.css` y `app.js`.
- `html comprobar ARCHIVO` es el "compilador": etiquetas sin cerrar o mal anidadas, ids repetidos, enlaces locales que no existen y la sintaxis de cada `<script>` en linea (con su linea). Salida `archivo:LINEA: error: texto`.
- `html empaquetar ARCHIVO [SALIDA]` mete CSS, JS e imagenes locales (hasta 300 KB) en un solo `.html`.
- `html abrir ARCHIVO` lo abre en el navegador de SOMA.

El nucleo es codigo puro (`src/apps/htmlproj.c`), sin dependencias del kernel; la orden (`src/agent/shell_html.c`) solo pone el VFS y el comprobador de JavaScript (`js -s`).

## El Programador
- Botones **JavaScript** y **HTML** en la pestana Nuevo; tambien se deduce de la descripcion ("en javascript", "con js", `juego.js`, "pagina web", "landing", `index.html`...). `.json` no cuenta como JS.
- **JavaScript**: referencia propia (win.*, input, fs, sin Node ni DOM), ejemplo de ventana y de consola, comprobacion de sintaxis con `js -s`, pruebas de consola con `js -x`, rechazo con pista de `require('fs')`, `alert`, `import`, `document.`...
- **HTML**: una pagina de un solo archivo; se rechazan CDN/recursos externos, `getContext` (el canvas 2D aun no existe en el navegador de SOMA), `<link>` y `<script src>`; el comprobador devuelve la linea de cada error y se repara.
- Lecciones aprendidas separadas por lenguaje (`/agent/coder_lessons_js.txt`, `..._html.txt`).
- En la lista, el editor y el boton Ejecutar: `.js` se lanza con `js`, `.html` con `html abrir`. Colores: JS amarillo, HTML naranja.

## Pruebas (`make pruebas-js`; no hace falta `make test`)
- `test_html_275`: 62 comprobaciones del nucleo (ASan/UBSan).
- `test_coder_js_275`: 68 comprobaciones del Programador con LLM y comprobadores falsos (mensajes reales de js y html_check).
- `test_html_manifiesto_275.py`: 24 comprobaciones del manifiesto.

## Limites honestos
No se pudo probar aqui (sin GUI/QEMU): el boton y la lista del Programador, `html abrir` en el navegador, la lectura del VFS y el comprobador de scripts en ring 3. Se compilan sin avisos y la logica esta probada en el anfitrion. Falta reentrenar el LCM con `html.json` (junto a cc, agenda y js).

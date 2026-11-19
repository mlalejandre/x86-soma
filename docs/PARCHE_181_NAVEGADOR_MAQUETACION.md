# Parche 181: Navegador (II) — maquetación y estilos

Requiere el parche 180. Objetivo: que las páginas reales (portales de noticias, Google, aplicaciones web) se
maqueten como en Chrome: tamaños, rejillas, flex, posicionamiento y colores modernos.

## Qué cambia

| Área | Novedad |
|---|---|
| Unidades | `dvh/svh/lvh/dvw/svw/lvw`, `lh/rlh`, `ch`; `fit-content`, `max-content`, `min-content` en `width` |
| Hoja de estilo del agente | márgenes y rellenos en `em` (como Chrome); botones, selects e inputs con `box-sizing:border-box` |
| Caja | `aspect-ratio`, `object-fit`, `object-position`, `-webkit-line-clamp`, `transform: translate/translateX/Y`, `translate` |
| Fondos | `background-repeat/position/size` corregidos (repetición por eje, desplazamientos, `cover/contain`) |
| Flex | columna con `flex-grow`/`flex-shrink` reales, `stretch` con segunda pasada, centrado vertical de botones |
| Rejilla | `grid-template-areas`, `grid-area`, filas explícitas (`grid-template-rows`), saltos de línea/columna, `repeat(auto-fill/auto-fit, minmax())`, `gap`, `justify-items/self` |
| Posición | `position:fixed` relativo a la ventana (antes a la página), `position:sticky` en pintado y detección de clics |
| Desplazamiento | cajas con `overflow:auto/scroll` se desplazan con la rueda (barra fina), `scrollTop` desde JavaScript |
| Colores | `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`, `color()`, `color-mix()`, `light-dark()`, `currentcolor`; propiedades lógicas (`margin-inline`, `inset`, …) |

Código nuevo: `src/apps/web_color.inc` (colores CSS 4 en coma fija, sin `libm`).

## Interruptor y modo seguro

* Parámetro de arranque `soma.weblayout=off` (o modo seguro): vuelve al comportamiento del parche 180
  (hoja de estilo antigua en px, sin rejilla con áreas, sin desplazamiento interno ni transformaciones).

## Pruebas

* `tests/test_web_layout_181.c` (anfitrión, con ASan/UBSan): rejillas con áreas y auto-fill, flex en columna,
  `aspect-ratio`, elementos fijos, unidades modernas, desplazamiento interno y colores CSS 4.
  Los valores esperados coinciden con `getBoundingClientRect()` de Chrome.
* `tests/layhost/lay_host.c`: volcador/pintor de maquetación en el anfitrion
  (`lay_host pagina.html [ancho alto salida.ppm [scroll [x,y,dy]]]`).
* `python3 parche_181_navegador_maquetacion.py --test`.

## Efectos a vigilar

1. El texto se redondea a pixel entero al colocar líneas (antes se truncaba): algunas líneas cambian 1 px.
2. La hoja de estilo en `em` altera ligeramente márgenes de `h1..h6`, `p`, `ul`, `blockquote`.
3. Los botones centran su etiqueta verticalmente.

## Límites conocidos

* Sin `rotate/scale/skew` (solo traslación), sin colores relativos (`rgb(from …)`), sin `max()/min()/round()` anidados con `%`.
* Las métricas de la fuente (DejaVu) son ~10 % más anchas que Arial/Helvetica: afecta a saltos de línea.
* Sin JavaScript de aplicaciones grandes (claude.ai): es el siguiente bloque.

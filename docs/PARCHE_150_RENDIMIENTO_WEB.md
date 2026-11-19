# SOMA 150 — Velocidad del navegador: imagenes y maquetacion

## Hallazgos concretos

- `web_layout.c::draw_image()` reserva un bitmap temporal del **tamano de toda la
  region visible** en cada repintado, y realiza una copia de pixeles incluso
  para imagenes a escala 1:1. Esto se agrava con scroll en paginas ricas en imagenes.
- `wp_tick()` puede invocar `relayout()` dos veces al terminar la ultima imagen.
- Procesa hasta dos descargas sin volver a consultar la cola de eventos; cada
  descarga individual continua siendo sincrona y puede tardar ~10 s.

## Cambios

1. Una imagen con mismas dimensiones origen/destino, radio cero y opacidad total
   se compone directamente desde el bitmap original con `fb_blit_alpha`;
   se conserva el blending del alpha y el recorte.
2. Los demas casos se rasterizan en franjas: temporales de 512 KiB como maximo
   (excepto una sola fila extremadamente ancha) en lugar de reservar toda la region.
   El algoritmo de remuestreo/color no cambia. Se reintenta con menos filas en OOM.
3. Las descargas se reparten por defecto una por turno del trabajador. Al terminar
   o quedarse sin imagenes visibles, la maquetacion pendiente se hace **una vez**.
4. Se incluye arnes con sanitizadores y tests de equivalencia para composicion
   directa, rangos de franjas y decisiones de maquetacion. Sintaxis C validada.

## Ajustes reversibles de compilacion

- `EXTRA_CFLAGS=-DSOMA_WEB_IMAGE_FASTPATH=0`: deshabilita la ruta rapida y los
  mosaicos; usa el mismo render de antes para diagnostico comparativo.
- `EXTRA_CFLAGS=-DSOMA_WEB_IMAGES_PER_TICK=2`: vuelve a dos cargas por turno.
- Para perfilado de repintados usar el registro ya introducido por el parche 149:
  `EXTRA_CFLAGS=-DSOMA_WEB_PERF_LOG=1` (manteniendo otras opciones si las hay).

## Seguridad y limitaciones

- No toca red, HTTPS, decodificadores, disco ni servicio `netsvc`.
- No es descarga HTTP asincrona: si un servidor tarda 10 segundos la tarea web
  seguira retenida mientras espera esa respuesta. Este problema requiere
  IO cancelable/asíncrona en un parche posterior.
- El arnes comprueba geometria/equivalencia algebraica; no sustituye QEMU ni la
  comparacion visual contra el navegador real y hardware fisico.
- Modifica `src/apps/web_layout.c` y `src/apps/web_page.c` (se enlazan al kernel),
  pero nunca activa/desactiva capacidades de red ni permisos Ring 3.

## Uso

`python3 parche_150_rendimiento_web.py --plan`
`python3 parche_150_rendimiento_web.py --self-test`
`python3 parche_150_rendimiento_web.py --apply`
`python3 parche_150_rendimiento_web.py --test`
`python3 parche_150_rendimiento_web.py --undo`

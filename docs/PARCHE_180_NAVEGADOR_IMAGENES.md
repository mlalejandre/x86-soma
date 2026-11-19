# Parche 180 — Navegador (I): WebP, SVG, ICO y animaciones

Requiere 179. Interruptor: `soma.webimg=off` (tambien el modo seguro) deja los formatos como antes.
Primer parche de la serie "navegador en serio": este se ocupa de las imagenes. La maquetacion y el motor de JavaScript van en los siguientes.

## Que anade
- **WebP**: con perdida (VP8), sin perdida (VP8L), con alfa y **animado**. Decodificador: `simplewebp` (Google / Miku AuahDark, BSD de 3 clausulas, derivado de libwebp; `src/apps/simplewebp.h`), solo enteros. El contenedor animado (ANIM/ANMF) se compone aqui. Se corrige un fallo de lectura al final de un trozo del original.
- **SVG**: rasterizador propio de enteros (`src/apps/web_svg.inc`): path completo (arcos incluidos), rect, circle, ellipse, line, polyline, polygon, g, use/symbol, transform, fill/stroke/opacidades, fill-rule, style y hojas `<style>` sencillas, currentColor, degradados lineales y radiales, viewBox y preserveAspectRatio. Sirve para `<img src=x.svg>`, fondos CSS, documentos SVG abiertos directamente y **`<svg>` en linea** (antes solo con la pasarela), incluidas las hojas de iconos (`<use href="#id">` a un simbolo de otro `<svg>`).
- **GIF animado** (disposiciones 1, 2 y 3, transparencia, entrelazado) y **WebP animado**: los fotogramas se guardan compuestos y el navegador repinta solo cuando toca y solo si la imagen esta a la vista.
- **ICO/CUR** (con PNG o BMP dentro).
- `<picture>` ya elige las fuentes WebP/SVG; solo se saltan AVIF/HEIC/JXL. Cabecera `Accept` de imagenes actualizada.
- Todo se decodifica en el proceso aislado `imgdec` (ring 3) en x86_64, como el resto de formatos.

## Correcciones de paso
- Las imagenes sin ancho/alto declarados y con borde o relleno se guardaban reducidas al tamano de su caja provisional (2-3 px) y se veian como bloques de color: ya no.
- Un fallo de red al bajar una imagen se reintenta una vez; si vuelve a fallar, el motivo lleva el codigo interno (`error de red rc=N`) en vez de "error de red 0".
- El kernel rozaba el limite de 14 MiB: el editor de codigo usaba cuatro buferes temporales de 256 KiB y ahora comparte uno (768 KiB menos de BSS); el servicio de PDF se compila sin los formatos web.

## Limites conocidos
- SVG: sin `<text>`, `<image>`, clipPath, mask, filtros, patrones, trazos discontinuos ni animaciones SMIL/CSS; las uniones de trazo son redondeadas.
- AVIF/HEIC/JPEG XL: no (hace falta un decodificador AV1/HEVC).
- Una animacion ocupa como mucho 3 millones de pixeles entre todos sus fotogramas: si no cabe se reduce (hasta 1/4) o se corta.
- En aarch64 (sin ring 3) los decodificadores corren en el kernel.

## Pruebas
`tests/test_web_img_180.c`: pixeles de WebP/GIF/ICO identicos a la referencia (Pillow/libwebp) y SVG por colores en puntos conocidos. Fuzzing en host con ASAN/UBSAN (miles de mutaciones por formato) sin errores.

# Parche 350 · Vídeo del navegador en el reproductor nativo y direcciones `data:`

**Hace falta `make`.** Requiere el 349.

## 1 · Clic en un `<video>`: reproductor nativo de SOMA

El navegador no reproducía vídeo: un `<video>` era un rectángulo negro con un triángulo (desde el 349, con su póster). Ahora,
al hacer clic en él, su MP4 se abre en el reproductor nativo de SOMA, el mismo de `youtube mp4 <dirección>`:

* H.264 + AAC, hasta 854×480;
* se descarga a trozos mientras se ve, se puede saltar, pausar y transcribir.

Cómo funciona:

* **Motor (ring 3, `web_page.c`).** El clic llega como siempre a la página. Si el JavaScript no lo cancela (`preventDefault`), se
  toma el `src` del vídeo o el primer `<source>` en MP4. Los HLS (`.m3u8`), WebM, Ogg, DASH y `blob:` no los sabe reproducir, y
  la barra de estado lo dice.
* **Paso al núcleo.** El motor deja la URL en la zona compartida con el núcleo (`w3_state.media_url`, con un contador
  `media_seq`), porque no puede abrir ventanas.
* **Ventana del navegador (núcleo, `browser.c`).** Solo acepta `http(s)`, sin espacios ni caracteres de control, de hasta 1000
  caracteres, y solo cuando el contador sube, es decir, tras un clic nuevo. Entonces abre el reproductor.

Probado en QEMU con `big_buck_bunny.mp4`: el clic abre la ventana del reproductor con el vídeo.

## 2 · `navegador data:…` y `about:`

La orden `navegador <dirección>` (y quien abre el navegador desde el escritorio) trataba todo lo que no empezaba por `http`,
`https`, `file` o `/` como una ruta del disco. `navegador data:text/html,<h1>Hola</h1>` abría `file:///data:text/html,…`
(«archivo no encontrado»). Ahora `data:` y `about:` se abren como direcciones, igual que en la barra de direcciones.

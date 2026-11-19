# Parche 179 — T480s: captura de pantalla, video y audio interno

Requiere 178. Interruptor: `soma.scrcap=off` (tambien el modo seguro).

## Que anade
- **Imagen**: guarda lo que se ve (el bufer de la GUI, sin el puntero) como PNG a tamano completo.
- **Video**: graba la pantalla como AVI MJPG. Codificador JPEG propio (`jpeg_enc.c`, baseline 4:2:0, solo enteros; resultado identico en calidad a libjpeg). Las tramas iguales (pantalla quieta) no gastan memoria.
- **Audio interno**: copia de lo que pasa por `audio_sys_write` (Media, `audio tone/play`, programas...). Se guarda mono a 48 kHz y se rellena con silencio donde no sono nada, para que el sonido no se desfase del video. Sale como WAV.
- **Video + audio**: AVI con las dos cosas.
- Aplicacion **CAPTURA** en el dock, ordenes `captura`/`cap` y atajos de teclado.
- Archivos en `/capturas`: `captura_NNN.png`, `pantalla_NNN.avi`, `audio_NNN.wav` (BIGFS; hace falta `big format yes` la primera vez).

## Uso
- App: botones IMAGEN, VIDEO, AUDIO, VIDEO+AUDIO (teclas P, V, A, B). RETARDO 0/3/5 s (cuenta atras para pasar a otra ventana: la de Captura saldria en la imagen), ESCALA 1/1..1/4, 5..24 imagenes/s. ABRIR ULTIMO abre el visor, el reproductor Media.
- La grabacion sigue aunque cierres la ventana de Captura.
- Teclado: **ImprPant** = imagen; **Control+ImprPant** = empezar/parar video con audio (aviso en pantalla).
- Shell: `captura estado|foto [ruta]|video [s]|audio [s]|av [s]|iniciar video|audio|av|parar|descartar|fps N|escala N|abrir`.
  Para grabar el audio de algo: `captura iniciar audio`, reproduce (por ejemplo `audio tone 600 3000`), `captura parar`.

## Limites
- Video en RAM (max 48 MiB y 30000 tramas) y audio en RAM (max 32 MiB, unos 5 min 49 s); se escribe al parar. Al llenarse se detiene.
- Por defecto escala 1/2 y 10 imagenes/s (en 2560x1440 da 1280x720). A escala 1/1 y mucha accion el video pesa y tarda mas en codificar: `captura estado` muestra ms por trama.
- Si cambia la resolucion de pantalla durante el video, las tramas con otro tamano se descartan.
- Solo el audio que pasa por el sistema de audio de SOMA (no el microfono).

## Otros cambios
- ImprPant tambien en teclados USB (HID 0x46).
- Los repintados de Captura, Camara y Grabadora (cronometro, nivel, imagen en vivo) ya no dependen de mover el raton: sus `tick` estaban dentro de `mouse_poll`; ahora se llaman tambien desde el bucle principal, como el Monitor del sistema.

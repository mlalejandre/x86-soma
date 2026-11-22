# Parche 253: transcribir en modo nativo (el sonido del vídeo, sin equipos externos)

Antes (252) en modo nativo el botón **Transcribir** decía que faltaba pasar el sonido del vídeo a whisper. Ahora lo hace.

## Cómo funciona

1. `job_whisper` (`youtube_ia.c`) comprueba whisper y el sitio del almacén igual que con el servicio.
2. `audio_native()` pide a YouTube la dirección del MP4 de 360p (la misma petición `player` que usa el reproductor), abre la
   descarga con el cliente HTTPS propio (`nsrc_fetch`) y arranca la extracción: `vpx_start()` en `vplay_sys.c`.
3. `vplay_sys.c` lanza una **segunda instancia** de `vplay.elf` (proceso `vpaudio`) con `mode = VP_MODE_AUDIO`. Es independiente
   de la que reproduce (otra región compartida, otro hilo, otro proceso): se puede transcribir con el vídeo en pantalla.
4. En modo audio, `vp_core.c` (`vp_extract`) recorre solo las muestras de audio del MP4 en orden, las decodifica con libxaac, las
   mezcla a mono y las reduce a **16 kHz por promedio de área** (filtra lo que sobra por encima de 8 kHz). La salida va a un anillo
   de 3 MiB que ocupa el sitio de los fotogramas (`VP_FR_OFF`). No abre el audio del sistema.
5. El hilo del núcleo recoge el anillo (validando `out_w`/`out_r`, que escribe el proceso) y lo pasa a un sumidero que escribe en
   BIGFS (`/yt_audio.wav`, cabecera de 44 bytes que se completa al final). Al acabar, se llama a `voz_run` como siempre y el WAV
   se borra.

El archivo del vídeo no se guarda: pasa por el anillo de entrada de 8 MiB. Como el MP4 de 360p lleva vídeo y audio entrelazados,
se descarga entero (unos 3-5 MiB por minuto) aunque solo se use el sonido; `youtube minutos N` limita cuánto se baja.

## Límites

- Hay que bajar el vídeo completo hasta el minuto pedido; YouTube no da una pista de solo audio con dirección directa.
- Una transcripción a la vez (whisper ya era así). Si la descarga se corta, se transcribe lo que llegó y se dice.
- Solo x86_64, como el reproductor nativo y whisper.

## Pruebas

- `tests/test_vplay_252.py`: casos `audio`, `audio` con red lenta y `audio5` (parar a los 5 s), con la duración correcta, sin
  errores y sin tocar el audio del sistema; y comparación con ffmpeg (relación señal/error ~32 dB, desfase fijo del relleno del códec).
- QEMU con YouTube de pega: el WAV sale de 16 kHz y con la duración del vídeo mientras el vídeo sigue reproduciéndose; el avance
  se ve en la pestaña Texto.

## No verificado

- Whisper real sobre ese WAV (en las pruebas se sustituyó whisper por una comprobación del WAV; el camino del WAV a whisper es
  el mismo que con el servicio) ni el rendimiento en el T480s.
- YouTube real desde SOMA (como en el 252).

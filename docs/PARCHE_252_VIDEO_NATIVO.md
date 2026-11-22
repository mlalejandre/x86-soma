# Parche 252 — Vídeo nativo y YouTube sin equipos externos

Hasta el parche 251, ver YouTube exigía otro equipo con `tools/yt_service.py` (yt-dlp + ffmpeg). Con este parche SOMA
**habla con YouTube directamente y decodifica el vídeo él mismo**. El servicio externo sigue existiendo como opción.

## Qué hace

| Pieza | Dónde | Qué |
|---|---|---|
| Reproductor nativo | `src/user/vplay/` → `vplay.elf` (ring 3) | Lee MP4, decodifica **H.264** (libavc) y **AAC** (libxaac), sincroniza con el reloj de audio |
| Lado del núcleo | `src/arch/x86_64/vplay_sys.c` (interfaz en `src/user/vplay/vp_abi.h`) | Lanza el proceso, le pasa el archivo por memoria compartida y recoge los fotogramas |
| Cliente HTTPS | `src/apps/yt_https.c` (interfaz en `yt_inner.h`) | HTTP/1.1 sobre el TLS 1.3 del sistema (certificado verificado), leyendo el cuerpo según llega, con `Range` |
| YouTube directo | `src/apps/yt_inner.h`, `src/apps/youtube_nativo.c` | Búsqueda, ficha, miniaturas y dirección del vídeo pedidas a YouTube |

En la aplicación YouTube no cambia nada a la vista: buscar, pulsar, ver, pausar, saltar. El modo nativo viene activado.

## Cómo funciona

```
buscar      POST https://www.youtube.com/youtubei/v1/search   (cliente WEB)      -> lista de vídeos
miniaturas  GET  https://i.ytimg.com/vi/<id>/mqdefault.jpg
abrir       POST https://www.youtube.com/youtubei/v1/player   (cliente ANDROID)  -> ficha + dirección del formato 18
vídeo       GET  https://rr…googlevideo.com/videoplayback  con Range: bytes=N-   (MP4 360p: H.264 + AAC)

 núcleo (hilo vplay)                         memoria compartida                      vplay.elf (ring 3)
 descarga ──► anillo de 8 MiB con el MP4 ─────────────────────────────────────────►  índice (moov) ► muestras
                                                                                     H.264 ► YUV ► XRGB
 ventana ◄── copia del fotograma ◄──────── dos búferes de imagen ◄────────────────  publica cada fotograma a su hora
 tarjeta de sonido ◄────────────────────── llamadas de audio de siempre ◄──────────  AAC ► PCM
```

- El proceso `vplay` **no puede abrir archivos ni red**: solo salir, dormir, mirar la hora y escribir audio (filtro de
  llamadas). Un vídeo corrupto o malicioso, como mucho, mata ese proceso; el núcleo comprueba todo lo que el proceso le dice
  (dimensiones, posiciones del anillo).
- No se ha tocado ningún archivo de `protected.txt` salvo la línea de ayuda de `youtube` en `src/agent/shell.c`: el reparto
  de memoria usa `usermode_exec_ex` tal como está.
- El vídeo no pasa por el disco: se descarga a medida que se ve (y se deja de descargar al pausar, cuando el anillo se llena).
- Saltar no vuelve a pedir nada a YouTube: el reproductor calcula en el índice el byte que necesita y la descarga sigue desde ahí.
- La dirección del vídeo solo se acepta si es `https://….googlevideo.com/…`.

## Límites (léelos)

- **Solo 360p.** Es el único formato que YouTube entrega hoy con dirección directa y sin JavaScript (formato 18). El resto
  va por su protocolo SABR. Para más calidad: `youtube modo servicio` (necesita el equipo externo).
- **Solo x86_64.** En aarch64 no hay reproductor nativo; allí sigue haciendo falta el servicio.
- **Puede dejar de funcionar.** YouTube cambia esto a menudo: de los cinco clientes probados el 21-11-2026 solo uno (ANDROID)
  daba una dirección utilizable. Si la búsqueda o los vídeos fallan con "YouTube ha respondido algo que SOMA no entiende" o
  "inicia sesión para confirmar que no eres un bot", hay que actualizar `src/apps/yt_inner.h` (y solo ese archivo):
  ejecuta `python3 tools/sonda_youtube_nativo.py --muestras` en cualquier equipo y mira qué responde YouTube ahora.
- **Sin subtítulos ni transcripción en modo nativo** (todavía): la ficha (pestaña Info), el resumen y las preguntas sí
  funcionan, con el título y la descripción. Transcribir con whisper y los subtítulos siguen pidiendo el modo servicio.
- Vídeos de hasta 854x480. Directos, vídeos con restricción de edad y los que piden iniciar sesión no se pueden ver.
- TLS: hace falta que la fecha y la hora de SOMA sean correctas (los certificados se verifican).

## Órdenes

```
youtube modo nativo|servicio     quién hace el trabajo (también en la caja de búsqueda: "modo nativo" / "modo servicio")
youtube mp4 <dirección>          reproduce cualquier MP4 (H.264 + AAC) de una dirección http(s)
youtube api auto|<base>          base de la API (solo para pruebas o un espejo propio); auto = https://www.youtube.com
youtube estado                   incluye el modo y los contadores del reproductor nativo
```

## Código de terceros

`third_party/libavc` (H.264) y `third_party/libxaac` (AAC), ambos de Ittiam y con licencia **Apache 2.0**, sin modificar
(solo se ha dejado fuera lo que no se usa: ver `README_SOMA.md` en cada carpeta). Unos 11 MB de fuentes; `vplay.elf` ocupa
2,3 MB dentro del núcleo. H.264 y AAC tienen patentes en algunos países: es asunto de quien distribuya SOMA.

## Pruebas

- `tests/test_mp4_252.c`: lector de MP4, con índices cortados y con bytes al azar (ASan).
- `tests/test_yt_inner_252.c`: lectura de respuestas **reales** de YouTube (`tests/datos_252/`, sin datos personales) y del cliente HTTP.
- `tests/test_vplay_252.py` + `tests/vplay_host_252.c`: el reproductor entero contra un núcleo simulado, con vídeos de
  ffmpeg: reproducción completa, red lenta, saltos, pausa, descarga cortada, fotogramas B, índice al final, sin audio.

## Verificado y no verificado

Verificado: compila en x86_64 y aarch64 con `WERROR=1`; las pruebas anteriores; la imagen decodificada del formato 18 real
de YouTube coincide con la de ffmpeg y el sonido difiere en menos de 1 unidad; en QEMU, con un "YouTube" de pega que
devuelve las respuestas reales y **a través de TLS 1.3**: búsqueda, miniaturas, ficha, reproducción a 30 fps sin fotogramas
perdidos, pausa y salto.
**No verificado**: contra el YouTube real desde SOMA (el entorno de desarrollo no tiene acceso). En concreto, que YouTube
acepte el TLS de SOMA y sus cabeceras, y el rendimiento en el T480s.

## Nota: el índice de fuentes (SRCFS) está casi lleno

SOMA lleva dentro una copia de su código (`src/`) con un índice de 511 entradas. Con este parche quedan **510 usadas**
(por eso varias cabeceras van juntas en `vp_abi.h` y en `yt_inner.h`). El próximo parche que añada archivos a `src/`
tendrá que ampliar ese índice (`src/fs/disk_layout.h`, que está en `protected.txt`) o fusionar archivos. `third_party/`
no cuenta.

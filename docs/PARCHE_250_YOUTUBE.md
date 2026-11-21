# Parche 250 — YouTube en SOMA

Aplicación para **buscar y ver vídeos de YouTube dentro de SOMA**: caja de búsqueda, resultados con miniatura y un reproductor
propio (pausa, saltos, calidad, siguiente). Funciona en QEMU sin configurar nada y en un PC real (T480s) apuntando a otro
equipo de la red.

## Por qué hace falta un servicio fuera de SOMA

El reproductor web de YouTube necesita cosas que SOMA no tiene: decodificadores H.264/VP9/AV1 y AAC/Opus, Media Source
Extensions y un motor de JavaScript rápido. Además YouTube protege las direcciones de los vídeos con desafíos en JavaScript
que cambian a menudo (por eso yt-dlp se actualiza continuamente).

Lo que SOMA sí tiene es un reproductor sólido de **AVI con vídeo MJPG y audio PCM** (`src/core/avi.c`, `src/core/jpeg.c`,
`src/apps/media.c`). El parche reparte el trabajo:

| Dónde | Qué hace |
|---|---|
| `tools/yt_service.py` (Mac/PC, con yt-dlp y ffmpeg) | Busca, sirve miniaturas JPEG y **convierte el vídeo a AVI MJPG+PCM mientras lo emite** |
| `src/apps/youtube.c` (SOMA) | La ventana: búsqueda, resultados, reproductor, controles; recibe el vídeo por la red |
| `src/apps/yt_proto.h` (SOMA) | Parte pura: protocolo, configuración y `yt_ring`, la ventana de memoria del vídeo |
| `src/apps/media.c` (SOMA) | El motor de SOMA Media aprende a reproducir **una fuente que crece** (`media_play_stream`) |

## Uso

**En QEMU** no hay que hacer nada: `ejecutar.py` arranca el servicio en `127.0.0.1:8097` (SOMA lo ve en `10.0.2.2:8097`).
Requisitos en el Mac: `brew install ffmpeg yt-dlp`. Sin yt-dlp el servicio arranca en **modo demo** (vídeos de prueba).

Abre el icono **YOUTUBE** del escritorio o escribe en el terminal:

    youtube                      abre la ventana
    youtube big buck bunny       abre la ventana y busca
    youtube estado               servicio, calidad, lo recibido y lo que hay en memoria
    youtube calidad 240|360|480
    youtube fps 10..30

**En un PC real** (T480s por USB), el servicio corre en otro equipo de la misma red:

    # en el Mac/PC
    python3 tools/yt_service.py --bind 0.0.0.0 --clave MICLAVE

    # en SOMA (terminal), una sola vez
    youtube servidor 192.168.1.20
    youtube clave MICLAVE

También se puede escribir `servidor 192.168.1.20` directamente en la caja de búsqueda de la ventana.
`youtube servidor auto` vuelve a usar la puerta de enlace (QEMU). La configuración se guarda en `/etc/secreto/youtube.conf`
(los agentes no pueden leer esa carpeta).

### Teclas

| Tecla | Acción |
|---|---|
| Espacio o `k` | Pausa / continuar |
| ← → (o `,` `.`) | Saltar 10 s |
| `n` / `p` | Vídeo siguiente / anterior de la lista |
| `q` | Cambiar la calidad (240 → 360 → 480p) |
| `/` o Tab | Ir a la caja de búsqueda |
| Esc o Retroceso | Volver a los resultados |
| Ctrl+V | Pegar en la caja de búsqueda |

## Cómo funciona por dentro

### El servicio (`tools/yt_service.py`)

    GET /yt/search?q=texto&n=12   ->  "SOMAYT 1 <n>" y una línea por vídeo:  id \t segundos \t título \t canal \t vistas
    GET /yt/thumb?id=ID           ->  JPEG de 168x94
    GET /yt/video?id=ID&t=S&h=360&fps=15   ->  AVI (MJPG + PCM 48 kHz estéreo) desde el segundo S, emitido en directo

- La búsqueda es `yt-dlp "ytsearchN:texto" --flat-playlist --dump-json`; solo pasan identificadores de vídeo válidos y el
  texto se limpia (sin tabuladores ni caracteres que las fuentes de SOMA no tienen).
- Para el vídeo, `yt-dlp -J` da las direcciones y sus cabeceras HTTP (se recuerdan 20 minutos) y ffmpeg las abre con `-ss`
  antes de `-i` (salto rápido). Si YouTube rechaza a ffmpeg, **plan B**: yt-dlp descarga y se lo pasa a ffmpeg por una tubería.
- ffmpeg solo escribe una cabecera AVI correcta al terminar. El servicio **monta el AVI él mismo**: cabecera fija con el
  número de fotogramas calculado con la duración, y detrás los trozos `00dc`/`01wb` tal como salen de ffmpeg (se descartan
  índices y relleno). Así SOMA sabe la duración desde el primer byte.
- Un solo vídeo a la vez: una petición nueva (otro vídeo o un salto) corta el ffmpeg anterior.
- Seguridad: el identificador se valida (`[A-Za-z0-9_-]{11}`), los programas se lanzan sin shell, por defecto solo escucha en
  `127.0.0.1`, y con `--clave` rechaza las peticiones sin ella.

### La ventana de memoria (`yt_ring`)

El vídeo no toca el disco (el `hdd.img` de QEMU tiene 64 MiB y un minuto a 360p son unos 34 MiB). Se guarda en 16 bloques de
1 MiB que forman una **ventana sobre el flujo**: desde un poco antes de lo que el reproductor está leyendo hasta lo último
recibido. Cuando se llena, suelta lo que el reproductor ya ha dejado atrás (lo más antiguo leído en los últimos segundos,
menos 1 MiB de margen: la pausa rebobina un poco el audio). En pausa nadie lee, no se suelta nada y la descarga se detiene:
el servicio deja de enviar. Los primeros 64 KiB (la cabecera AVI) se conservan aparte.

**Saltar** es pedir el vídeo otra vez desde otro segundo: el servicio lo vuelve a emitir desde ahí (1–2 s).

### El reproductor (`media.c`)

`media_play_stream()` recibe tres funciones (`read`, `avail`, `eof`) en lugar de un archivo del BIGFS:

- el final de `movi` es «lo que ha llegado» y se actualiza en cada lectura;
- un trozo a medio llegar no se salta: se reintenta;
- sin datos, el reproductor pasa a «esperando» y **reanuda con unos 2 s de margen** (medidos con el ritmo real del vídeo);
  el reloj de pared no cuenta la espera;
- la pausa recoloca el audio con **puntos de control** (los últimos 256 trozos de audio) en vez de recorrer el archivo desde
  el principio, que en un flujo ya no está en memoria;
- `media_frame()` entrega el último fotograma a la ventana de YouTube y `media_keepalive()` evita que el reproductor se pare
  por «ventana cerrada».

Los archivos del BIGFS se reproducen exactamente como antes.

## Límites conocidos

- **Depende del servicio**: sin el Mac/PC no hay YouTube. Reproducir el formato original en SOMA exige decodificadores
  H.264 y AAC en ring 3 (siguiente etapa).
- Sin cuenta de Google: no hay suscripciones, historial ni recomendaciones personalizadas.
- Calidad: hasta 480p y 30 fps. A 360p y 15 fps son unos 0,6 MiB/s por la red.
- Los directos pueden no abrirse (dependen del formato que dé YouTube).
- Vídeos de más de ~2 horas se cortan (los desplazamientos del lector AVI son de 32 bits); un salto reinicia la cuenta.
- En aarch64 (Raspberry Pi 5) compila y reproduce sin sonido (aún no hay audio en esa plataforma).
- Descargar vídeos con yt-dlp va contra las condiciones de uso de YouTube: es para uso personal.

## Pruebas

- `tests/test_youtube_250.c` (en `make test`): resultados, cabeceras HTTP, direcciones, configuración y `yt_ring` con un
  reproductor simulado de dos cursores, pausas y rebobinados; y datos al azar con ASan/UBSan.
- `tests/test_yt_service_250.py` (en `make test`; se salta sin ffmpeg): el AVI emitido pasa por `tests/avicheck_250.c`, que
  usa el lector AVI y el decodificador JPEG de SOMA; camino real con un yt-dlp de pega y un «YouTube» local.
- En QEMU, con el servicio en modo demo: `python3 tools/yt_service.py --demo` y `youtube prueba`.

# Parche 315 · YouTube nativo: sus respuestas se leen fuera del núcleo

Tercer paso para sacar de ring 0 el código que trata datos de fuera (después del correo, 311, y de lo que lee el agente, 312).
**Hace falta `make`.**

## El problema
La aplicación YouTube en modo nativo (parche 252) habla directamente con youtube.com por HTTPS. Las respuestas son JSON grandes
(cientos de KB la búsqueda), y el núcleo buscaba en ellas los vídeos, la ficha y la dirección del MP4 (`yti_parse_search` y
`yti_parse_player_ex`, en `yt_inner.h`). Una respuesta preparada para romper ese análisis rompía el sistema. Además, la dirección
del vídeo que sale de ahí se usa después en la línea de la petición HTTP.

## Qué cambia
* **`leesvc.elf`** (el lector aislado del parche 312) aprende dos operaciones: `SBX_LEE_YT_BUSCA` y `SBX_LEE_YT_FICHA`. Ejecutan
  **el mismo código de antes** (`yt_inner.h`). No se añade otro programa a la imagen.
* **`src/apps/yt_aislado.c`** (nuevo): `yti_busca_aislado` y `yti_ficha_aislada`, que usa `youtube_nativo.c`. El núcleo exige que
  el tamaño de la respuesta cuadre exacto y **sanea** lo que devuelve (`yt_aislado.h`, puro):
  * como mucho los resultados pedidos; cada id con sus 11 caracteres válidos (si no, el resultado se quita);
  * todas las cadenas terminadas dentro de su campo y sin caracteres de control (la descripción conserva los saltos de línea);
  * la dirección del vídeo, **solo ASCII visible** y de `*.googlevideo.com` (o de la base propia de pruebas, `conf.api`): si no,
    se descarta. Así no se pueden colar cabeceras con `\r\n` en la petición;
  * «se puede ver» se recalcula a partir del estado.
* Si el lector falla, la aplicación dice «No se ha podido leer la respuesta de YouTube». No se reintenta en el núcleo.
* En aarch64 (sin EL0) y en las pruebas del anfitrión se llama directo, como antes.
* `yt_aislado.h` y `yt_aislado.c` quedan protegidos frente a la IA (91 = 91).

## Fallo que ya estaba, arreglado
`yti_str` (descodifica una cadena JSON) devolvía 0 cuando la cadena no se cerraba antes del final (una respuesta cortada), pero
dejaba lo escrito **sin terminador**. Quien no miraba ese 0 (el canal de un resultado; el estado, el motivo, el título y el
autor de la ficha) se quedaba con un campo que, leído como cadena, seguía por lo que hubiera detrás en la memoria. Ahora la
salida queda siempre terminada. Lo arregla también en aarch64.

## Pruebas
* **QEMU** (`make sbx-check`): una búsqueda (escapes, emoji con par subrogado, UTF-8 crudo, un id repetido y uno no válido), las
  fichas reales de `tests/datos_252` (un vídeo y «no eres un bot») y una ficha con una dirección que lleva `\r\n`: el servicio
  da lo mismo que el núcleo, la dirección con `\r\n` no llega y nunca se pasa de lo pedido. **300 respuestas mutiladas** dan lo
  mismo (0,75 ms por respuesta en QEMU) y el núcleo sobrevive. Sin pérdidas de memoria.
* **Anfitrión** (`tests/test_yt_aislado_315.c`): las respuestas reales cortadas en cualquier punto dejan todos los campos
  terminados; 20 000 estructuras al azar salen del saneado seguras; con respuestas legítimas el saneado no cambia nada.
  `test_yt_inner_252` y `test_youtube_250` siguen pasando.

Lo que sigue en el núcleo: la descarga (TLS y HTTP, `yt_https.c`), como en el parche 312. El vídeo ya lo decodifica `vplay`
en ring 3.

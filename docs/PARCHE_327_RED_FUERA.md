# Parche 327 · Ningún cliente de red en el núcleo

**Hace falta `make`.** Cierra lo que empezaron los parches 316, 317, 325 y 326 (sacar la red del núcleo).

## El problema
Después del 326 todavía quedaban en x86_64 cuatro sitios que abrían conexiones desde ring 0 con el cliente HTTP mínimo del núcleo
(`http_get` / `http_post_json`) o con TCP directo, y leían en ring 0 lo que mandaba el otro lado:

| Quién | Qué hacía | Dónde |
|---|---|---|
| LLM local sin clave (`http://IP:puerto`) | `GET /health` y `POST /v1/chat/completions` (agente y aplicación de chat) | `src/agent/llm.c` |
| Memoria | `POST /v1/embeddings` | `src/agent/memoria.c` |
| Servicio de YouTube de casa | TCP en claro: búsquedas, fichas, vídeo e IA | `src/apps/youtube.c`, `youtube_ia.c` |
| Órdenes `search` y `curl` | `http_get_host` al proxy de QEMU o a cualquier servidor | `src/agent/shell_red.c` |

## Qué cambia
* **`wn_http_simple`** (nuevo, `src/apps/web_net_sbx.c`): un GET o un POST JSON por `netsvc` con el mismo contrato que
  `http_get` / `http_post_json` (cuerpo en el búfer del que llama, con NUL; estado HTTP o < 0). Así no cambia nada de lo que hay
  alrededor.
* **`llm_http_local`** (nuevo, `llm.c`): el servidor local del LLM y el de embeddings de la memoria van por `wn_http_simple`. El
  chat se puede **cancelar** (antes esperaba hasta el final). En aarch64 (sin EL0) sigue el cliente mínimo, como antes.
* `netsvc` admite peticiones de hasta **10 minutos** (antes 60 s), lo que tarda un modelo local grande. Esto es lo que había
  hecho que el 325 dejara fuera el LLM local.
* `llm_health` ya no retiene el cerrojo de red mientras espera: con la petición en `netsvc`, que coge ese cerrojo en cada llamada
  al sistema, se bloqueaba la red entera hasta el plazo.
* **YouTube de casa**: `youtube.c` abre la conexión con `tun_abre` (el túnel de ring 3 del 326, sin TLS) y lee y escribe por él.
  El resto del cliente no cambia: los asas enteros de siempre son índices de una tabla pequeña.
* **`search` y `curl`** van por `netsvc` (en aarch64, como antes).

Con esto, en x86_64 **ningún programa del sistema abre conexiones ni lee respuestas de la red en ring 0**: el HTTP, el TLS y los
certificados van en procesos de ring 3, uno por petición o por conexión.

### Lo que sigue en el núcleo, dicho claro
* La **pila de red**: el controlador de la tarjeta, IP, TCP, UDP, ARP, DHCP y DNS. Sacarla es rediseñar el sistema como un
  micronúcleo, no un parche.
* Los **analizadores de protocolo** que trabajan sobre lo que devuelve el túnel: IMAP y SMTP del correo, el HTTP de `yt_https.c` y
  las cabeceras del servicio de YouTube (`yt_proto`). Son puros y ya tienen pruebas con datos hostiles (parches 252, 295 y 319).
* **aarch64** no tiene EL0 todavía: allí todo esto sigue en el núcleo.
* `wn_fetch` (el cliente del navegador en el núcleo) sigue compilado, porque es el camino de aarch64. En x86_64 nadie lo llama
  para las aplicaciones.

## Pruebas
* **QEMU** (`make sbx-check`, bloque `SBXTLS`): `tools/sbx_check.py` simula en el anfitrión un **LLM local** (sin clave, tarda
  1,5 s), un **servidor de embeddings** y un **servicio de YouTube**. Se comprueba, contando los procesos aislados que se lanzan,
  que todo va por ring 3:
  * la salud del LLM local (`GET /health`);
  * el agente (`llm_chat`) y la aplicación de chat (`llm_chat_hist`) con el LLM local lento;
  * cancelar el chat corta la espera (35 ms);
  * con el servidor apagado, «el servidor local no responde»;
  * un embedding de 16 dimensiones para la memoria;
  * una búsqueda en el servicio de YouTube por el túnel, y el mensaje claro si el servicio está apagado;
  * la orden `curl` por `netsvc`;
  * sin pérdidas de memoria.
* Compila sin avisos en x86_64 y aarch64.

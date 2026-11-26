# Parche 325 · El cliente del LLM, fuera del núcleo

**Hace falta `make`.** Sigue a los parches 316 y 317.

## El problema
Después del 317, el cliente HTTP y TLS del núcleo (`wn_fetch`) solo lo usaba el cliente del LLM (`llm.c`): proveedores en la nube
(OpenAI, Claude, Gemini), servidores con nombre o con clave, el lanzador y la lista de modelos. El apretón TLS, los certificados y
la respuesta HTTP de ese servidor se analizaban en ring 0. No se había movido antes porque no había forma de probarlo.

## Qué cambia
* `netsvc` acepta **cabeceras propias** (`hdr_len` en `struct sbx_net_req`, como mucho 4 KiB): la clave de la API.
* `llm_fetch` y el camino del chat (con cancelación) usan `wn_fetch_isolated`: **un proceso nuevo por petición** que termina con
  ella (parche 316), así que la clave no se queda en ningún proceso. El núcleo **borra** el búfer de la petición antes de
  liberarlo, y las páginas del proceso ya se reservaban a cero.
* Sin cambios: el servidor local **sin clave** por `http://IP:puerto` sigue por el camino de siempre (`http_post_json`), el que
  funciona en la red de casa con llama.cpp. En aarch64 todo sigue igual.
* El mensaje de error ya no añade el último error de TLS del núcleo (no era de esta petición): el motivo llega desde `netsvc`.

Con esto, **ninguna descarga del sistema usa ya el cliente HTTP/TLS del núcleo** salvo YouTube nativo (`yt_https.c`, que lee el
vídeo según llega) y el correo (IMAP/SMTP).

## Pruebas
* **QEMU** (`make sbx-check`, dentro de `SBXTLS`): `tools/sbx_check.py` sirve un falso `/v1/chat/completions` por HTTPS con la
  autoridad de prueba, que exige `Authorization: Bearer sk-prueba-325` y el modelo en el cuerpo. Con el proveedor OpenAI apuntando
  a él, `llm_chat("hola")` devuelve «hola desde el servidor falso» y lo hace por `netsvc` (las estadísticas de procesos aislados
  suben); con otra clave falla y lo dice («el proveedor rechaza la clave: Incorrect API key»). La configuración se restaura al
  terminar.
* El resto de `sbx-check` sigue bien. Compila sin avisos en x86_64 y aarch64.

# Parche 311 · El correo se lee fuera del núcleo (servicio aislado en ring 3)

Primer paso para sacar las aplicaciones de ring 0. **Hace falta `make`.**

## Por qué el correo primero
Un correo es lo más hostil que entra en SOMA: lo escribe cualquiera en Internet y llega tal cual. Leerlo exige decodificar
MIME, base64, quoted-printable, juegos de caracteres, palabras RFC 2047 y HTML. Hasta ahora todo eso corría **en el núcleo**:
un fallo en cualquiera de esos analizadores era un fallo del sistema entero, y uno explotable daba el control de la máquina con
solo recibir un correo.

## Qué cambia
* **`mimesvc.elf`** (nuevo, `src/user/mimesvc.c`): un servicio aislado como los de imágenes, PDF e inflate (parches 138-142).
  Solo puede llamar a `exit`; no ve la memoria del núcleo ni la de otros procesos, no tiene red ni disco, tiene plazo, su montón
  tiene tope y se pone a cero entre peticiones. Ejecuta **el mismo código de antes** (`correo_core.c`) para tres cosas:
  * leer un mensaje entero (`co_mime_lee`): cabeceras, texto legible y lista de adjuntos;
  * decodificar un adjunto (`co_adj_saca`);
  * la ficha de cada correo en la lista (`co_ficha`: cabeceras, extracto y si tiene adjuntos). Antes estaba en `correo_red.c`.
* **El núcleo no se cree la respuesta** (`src/apps/correo_sbx.h`, funciones puras):
  * cierra cada cadena y deja los contadores en rango;
  * descarta los adjuntos cuyo trozo no cae dentro del mensaje;
  * el texto nunca pasa de lo pedido;
  * `uid`, banderas y tamaño los pone el núcleo (vienen del servidor IMAP), no el servicio.
* Si el servicio se detiene con un mensaje (está mal formado o es malicioso), **no se reintenta en el núcleo**. La aplicación
  enseña «SOMA no ha podido leer este correo de forma segura...», la lista lo muestra como «(correo ilegible)» y el resto del
  sistema sigue igual.
* `correo.c` y `correo_red.c` llaman a `correo_mime_lee`, `correo_adj_saca` y `correo_ficha` (`src/apps/correo_sbx.c`).
* En aarch64 (todavía sin modo usuario, EL0) esas tres funciones llaman directamente a `correo_core.c`, como antes. También en las
  pruebas del anfitrión (`WEB_HOST_TEST`).
* `mimesvc.c`, `correo_sbx.h` y `correo_sbx.c` quedan protegidos frente a los parches de la IA (`protected.txt` y
  `agent_protegidos.h`: 83 = 83).

Lo que sigue en el núcleo del correo: IMAP y SMTP (`correo_red.c`), y leer las respuestas del servidor (líneas, literales y
FETCH). Son analizadores mucho más pequeños, y la conexión TLS es del núcleo. Sacarlos es el paso de llevar la aplicación entera a
ring 3, con ventana, red y disco propios.

## Pruebas
* **En QEMU** (`make sbx-check`, en la CI), sección nueva de `sbx_selftest`:
  * 3 mensajes reales (multiparte con adjunto, latin1 y RFC 2047; HTML en base64; sin MIME) dan **exactamente** lo mismo en
    el servicio que con `correo_core.c` en el núcleo: cabeceras, texto byte a byte, adjuntos y ficha de la lista;
  * el adjunto decodificado es idéntico, nunca pasa del tamaño pedido, y un adjunto fuera del mensaje se rechaza;
  * **300 mensajes mutilados** (bytes cambiados, cortes, basura): los 300 dan lo mismo y el núcleo sobrevive;
  * tras parar los servicios, el siguiente correo vuelve a arrancar el lector;
  * no se pierde memoria (marcos libres antes = después, montón del núcleo sin crecer).
* **Coste**: 100 fichas en 77 ms en QEMU sin aceleración (0,8 ms por correo al sincronizar la lista).
* **En el anfitrión** (`tests/test_correo_sbx_311.c`, en `make test`): 20 000 estructuras de bytes al azar pasan por el saneado y
  salen siempre seguras; con mensajes buenos, el saneado no cambia ni un byte.

# Parche 316 · Lo que el agente descarga de Internet, fuera del núcleo

Cuarto paso para sacar de ring 0 el código que trata datos de fuera. Después de leer el correo (311), las páginas (312) y
YouTube (315) en servicios aislados, quedaba **la descarga**. **Hace falta `make`.**

## El problema
`investiga` y `search` descargan con `wn_fetch`, el cliente HTTP del núcleo. Así que el núcleo hacía el apretón TLS 1.3, analizaba
los **certificados X.509 (ASN.1)** y la respuesta HTTP (cabeceras, chunked, gzip, redirecciones) de **cualquier servidor** al que
llegue el agente siguiendo los resultados de una búsqueda. Era la mayor superficie de ataque que quedaba en ring 0. Además, esas
peticiones llevaban las cookies del navegador a cualquier sitio.

Ya existía `netsvc` (parche 142), el mismo cliente HTTP en un proceso aislado de ring 3 para la orden `descargar`, pero **sin
TLS**: el HTTPS solo funcionaba a través de la pasarela de QEMU. En un equipo real, `descargar https://...` fallaba.

## Qué cambia
* **`netsvc.elf` lleva TLS 1.3** (`tls13.c`, `x509.c`, `tlscrypto.c`, `crypto.c` y las raíces de Mozilla), como el navegador
  aislado (`websvc`). Sigue sin estado entre peticiones (el montón se vacía en cada una y no guarda cookies).
* Con cada petición el núcleo le manda lo que el proceso no puede saber por sí mismo (`struct sbx_net_req`, `sbx_abi.h`):
  * 32 bytes de **azar** del generador del núcleo (para las claves efímeras de TLS), nuevos en cada petición;
  * la **hora** (para las fechas de los certificados), si hay que **verificar** (`soma.tlsverificar=off` sigue valiendo) y si la
    red es real (HTTPS directo) o QEMU (pasarela);
  * las **autoridades del usuario** (`/etc/tls_confianza.pem`, leídas en un búfer propio);
  * y ahora también `Accept` y `direct` (TLS propio aunque haya pasarela), que usa el agente.
* **Cada descarga va en un proceso `netsvc` nuevo** que termina con ella (antes era un servicio persistente que atendía una
  petición tras otra). Si una respuesta hostil llegara a tomar el proceso, no vería ni podría alterar las descargas siguientes
  (lo próximo que lea el agente, otra descarga). En QEMU sin aceleración cuesta de 15 a 80 ms más por descarga; con la red
  de por medio no se nota.
* `investiga` y `search` (`shell_investiga.c`, `shell_red.c`) descargan con `wn_fetch_isolated` (5 llamadas).
* `wn_fetch_isolated` rechaza lo que `netsvc` no sabe hacer (cabeceras propias, cancelación, `Referer`) en lugar de ignorarlo.
* En aarch64 (sin EL0) `wn_fetch_isolated` es el cliente de siempre.
* Si `netsvc` no puede con TLS, el error lo dice («TLS: el certificado no lo firma ninguna autoridad de confianza»).
* `netsvc.c` y `web_net_sbx.c` quedan protegidos frente a la IA (93 = 93).
* La imagen crece 152 KB (del 60,4 % al 60,7 %).

Lo que **sigue en el núcleo**: el visor de documentos (`web_visor.c`, porque se cancela al cerrar), el cliente del LLM
(`llm.c`, `chat.c`: lleva la clave de la API) y la orden de diagnóstico `tls`. Son los pasos siguientes.

## Pruebas
* **QEMU** (`make sbx-check`, nuevo bloque `SBXTLS`): `tools/sbx_check.py` crea con `openssl` una autoridad de prueba y un
  certificado para `10.0.2.2`, sirve las rutas de prueba de `netsvc` también por **HTTPS (solo TLS 1.3)** y pasa la autoridad
  al núcleo. El núcleo la pone en `/etc/tls_confianza.pem` y:
  * texto, gzip, chunked y una redirección por HTTPS dan **lo mismo** por `netsvc` que por el cliente del núcleo;
  * sin la autoridad, el certificado se **rechaza** en los dos;
  * con `tls13_verify = 0` (lo que hace `soma.tlsverificar=off`) `netsvc` cifra sin verificar, como el núcleo;
  * sin pérdidas de memoria.
  Sin `openssl` en el equipo, esa parte se omite avisando. Probado con OpenSSL 3; las opciones que usa existen también en
  LibreSSL (la del Mac), pero allí no lo he podido probar.
* Lo de antes sigue igual: `SBXNET` (HTTP, gzip, chunked, redirección, binario de 300 KB, capacidades de red).

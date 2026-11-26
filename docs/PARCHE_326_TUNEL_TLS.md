# Parche 326 · El correo y YouTube, sin TLS en el núcleo

**Hace falta `make`.** Sigue a los parches 316, 317 y 325 (sacar la red del núcleo).

## El problema
Después del 325, el núcleo de x86_64 solo hacía TLS en dos sitios: el **correo** (IMAP y SMTP, también con STARTTLS: `correo.c`) y
**YouTube nativo** (`yt_https.c`: youtube.com y googlevideo.com). Ahí seguían en ring 0 el apretón TLS, el análisis de los
**certificados X.509** y el descifrado de lo que manda el servidor. No servía `netsvc`: los dos necesitan una **conexión viva**
(una sesión IMAP; un vídeo que se lee según llega), no una petición y una respuesta.

## Qué cambia
* **`tlstun.elf`** (nuevo, `src/user/tlstun.c`): un túnel TLS de ring 3, **un proceso por conexión**, con capacidad de red y
  nada más. Hace el TCP, el TLS 1.3 (con la misma verificación de certificados, las autoridades del usuario y
  `soma.tlsverificar`) y **STARTTLS**. El núcleo le pasa y recibe **texto en claro** por dos anillos de 64 KiB en una región
  compartida (`src/core/tun_abi.h`).
* **`src/apps/tun_tls.c`** (nuevo): `tun_abre`, `tun_starttls`, `tun_envia`, `tun_recibe`, `tun_cierra`. El núcleo no se fía
  de los contadores del anillo (si el proceso miente, la conexión da error). En aarch64 (sin EL0) hace el TCP y el TLS aquí
  mismo, como antes.
* `sandbox.c`: `sbx_lanza` (lanzar un proceso aislado **sin esperar**, con una región compartida) y `sbx_termina`.
* El transporte del correo (`correo.c`) y el cliente de YouTube (`yt_https.c`) usan el túnel. El IMAP, el SMTP y el HTTP siguen en
  el núcleo: son puros y ya tienen pruebas con datos hostiles (parches 252, 295 y 319).
* **16 procesos de usuario a la vez** (antes 8): durante YouTube pueden estar vivos el reproductor, los servicios aislados y varias
  conexiones. Cuesta unos 725 KiB de memoria fija; la imagen queda en el 58,2 % de su presupuesto.
* `tlstun.c`, `tun_tls.c`, `tun_tls.h` y `tun_abi.h` quedan protegidos frente a la IA (97 = 97).

Con esto, en x86_64 **ningún programa del sistema hace TLS en el núcleo**, y el apretón y los certificados de los servidores van
en ring 3. Queda el LLM local **sin clave** (`http://IP:puerto`, sin TLS), que sigue con el cliente HTTP mínimo del núcleo. No lo he
movido: en `netsvc` las peticiones tienen un plazo de 60 s y un modelo local puede tardar 5 minutos, y es justo el camino que el
parche 286 dejó así para que funcione en la red de casa, que no puedo reproducir.

## Pruebas
* **QEMU** (`make sbx-check`, bloque `SBXTLS`):
  * una página entera por HTTPS a través del túnel, con el certificado verificado, en un proceso de ring 3;
  * el **cliente de YouTube nativo** (`yh_open` y `yh_read`) de punta a punta por el túnel, con respuesta chunked;
  * **seis túneles a la vez** con los servicios aislados vivos;
  * **STARTTLS** contra un servidor de prueba al estilo de SMTP o IMAP: saludo en claro, `STARTTLS`, TLS 1.3 y eco;
  * sin la autoridad de prueba, el certificado se rechaza («el certificado no lo firma ninguna autoridad de confianza»);
  * sin pérdidas de memoria.
* **QEMU**: `make fault-check` (21 de 21), `prueba_python_237` 15/15, `prueba_progz_314` 6/6.
* **Anfitrión**: siguen pasando las pruebas de correo (154/154, 46/46, 31/31, 81/81, 17 y 4 comprobaciones) y de YouTube.
* Compila sin avisos en x86_64 y aarch64, y `tlstun.c` y `tun_tls.c` también con clang.

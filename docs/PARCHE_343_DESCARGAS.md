# Parche 343 · El navegador descarga en paralelo y reutiliza las conexiones

**Hace falta `make`.** Requiere el 342. Son las causas 1 y 5 de la lista de lentitud del navegador.

## 1 · Varias descargas a la vez

El proceso del navegador (ring 3) no tiene hilos. Desde el parche 205 las descargas eran síncronas: una imagen detrás de otra,
y cada una pagaba entera la latencia hasta el servidor.

* **Corrutinas de descarga** (`src/user/web_dl.c`, `src/apps/web_dl.h`):
  * hasta 4 descargas a la vez, cada una con su pila;
  * reutilizan el cambio de pila de las corrutinas de JavaScript (`soma_co_switch`);
  * el código de red no cambia.
* **Ceder al esperar.** Cuando una descarga espera a la red (`unet_idle`, `web_yield`), cede el turno al bucle principal, que se
  lo da a las demás. Cuando el bucle principal espera (una hoja de estilo, un script, la página), también avanzan las descargas.
* **Imágenes** (`web_page.c`):
  * el cargador lanza las pendientes más cercanas a la vista mientras haya hueco y recoge las terminadas;
  * decodificar se sigue haciendo en el bucle principal;
  * cada trabajo lleva su copia de la URL y del referente;
  * al recogerlo se comprueba que la página, la imagen y la URL siguen siendo las mismas (navegar a mitad no mezcla nada).
* **Lo que no es http(s)** (`data:`, SVG en línea, archivos) se sigue cargando en síncrono, como antes.
* **El arm64**, sin proceso aislado, no cambia.

## 5 · Conexiones reutilizadas (keep-alive)

Cada petición abría su conexión TCP (y en la red real su TLS 1.3 entero, con la verificación de certificados) y la cerraba.

* **Qué se guarda** (`web_net.c`). Una respuesta completa (`Content-Length` o trozos), en HTTP/1.1 y sin `Connection: close`,
  deja la conexión guardada hasta 15 s.
* **Cuándo se reutiliza.** La siguiente petición GET al mismo servidor (IP, puerto y nombre TLS) la usa; las peticiones GET ya
  piden `Connection: keep-alive`.
* **Si el servidor ya la había cerrado,** la petición se repite con una conexión nueva. Solo GET: un POST nunca se repite.
* **Límites.** Como mucho 4 guardadas; el bucle del navegador cierra las caducadas.
* **Solo en el proceso del navegador.** En el núcleo (la autoprueba de x86_64 y el navegador de arm64) y en `netsvc` todo
  sigue como antes: una conexión por petición, con `Connection: close`. El núcleo no tiene un bucle que cierre las caducadas.
* **No sobrevive a un cambio de confianza.** Una conexión TLS guardada solo se reutiliza mientras no cambien las autoridades del
  usuario (`/etc/tls_confianza.pem`) ni la opción de verificar. Si cambian, se abre otra y se vuelve a comprobar el certificado.
  Lo encontró la prueba del servicio aislado (`sbx-check`): quitar la autoridad de prueba y que la siguiente petición siga
  funcionando por la conexión ya verificada.
* **La pasarela** (`tools/web_gateway.py`, versión 6) habla HTTP/1.1 con SOMA:
  * toda respuesta lleva `Content-Length`;
  * si algo falla sin responder, o queda un cuerpo sin leer, cierra la conexión, para que nunca se mezclen dos peticiones.
* **Núcleo:** 16 conexiones TCP en lugar de 8 (`tcp.c`). El navegador usa hasta 4 a la vez más las guardadas, y el resto del
  sistema (correo, LLM, `netsvc`, panel web) sigue teniendo sitio.

## Medido en QEMU

Portada de prueba con 120 fotos detrás de un retardo de 25 ms en cada sentido (50 ms de ida y vuelta, como hasta un CDN):

| | 342 | 343 |
|---|---|---|
| HTTP directo: tiempo | 14,4 s | **5,4 s** |
| HTTP directo: conexiones abiertas con el servidor | 121 | **4** |
| HTTPS por la pasarela: tiempo | 4,8 s | **3,9 s** |

Por la pasarela, la versión 341 tardaba 22,7 s en la misma prueba. La pasarela reutilizó 109 de sus 121 conexiones con el origen.

## Pruebas

* `tests/test_web_dl_343.c` (en `make test`; solo en Linux x86_64, por el cambio de pila):
  * como mucho 4 a la vez;
  * van por turnos, no una detrás de otra;
  * cada una ve su propia pila y la conserva entre cesiones;
  * al volver se restaura la pila en curso;
  * desde dentro no se anidan;
  * los huecos se reutilizan.
* `tests/test_pasarela_342.py` sigue pasando con la pasarela en HTTP/1.1.
* `tests/prueba_navegador_341.py` (QEMU): las 120 fotos con 4 GiB y con 512 MB.

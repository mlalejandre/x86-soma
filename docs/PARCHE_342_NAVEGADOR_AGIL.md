# Parche 342 · El navegador carga las páginas con fotos mucho más rápido

**Hace falta `make`.** Requiere el 341. Son las causas 3, 4 y 2 de la lista de lentitud del navegador.

## Dónde se iba el tiempo

Portada de prueba con 120 fotos de 990×556 vistas a 240 px de ancho, medida en QEMU con el motor instrumentado:

| Paso | Antes | Por qué |
|---|---|---|
| Decodificar JPEG | 10,0 s (83 ms por foto) | se decodificaban a tamaño completo para verlas a 240 px |
| Repintar | 5,5 s (121 × 45 ms) | cada imagen que llegaba repintaba el fotograma entero |
| Red local | 0,8 s | |
| Maquetar | 0,08 s | |

## Qué cambia

### 3 · Decodificar al tamaño al que se ve

* La maquetación le pasa al cargador el ancho y el alto que fijan el CSS o los atributos de cada `<img>` (`wimginfo.fixed_w`/`fixed_h`).
  Con el ancho fijo y el alto automático (lo habitual en las portadas) la caja era provisional y la foto se decodificaba entera.
  Ahora el JPEG sale ya reducido a 1/2, 1/4 u 1/8, nunca por debajo de lo que se ve.
* Si la misma imagen se muestra en otro sitio a su tamaño natural, no se reduce (`sin_fijo`), para no pixelarla.
* **IDCT reducida** (`web_jpeg.c`). Al reducir se hacía la IDCT 8×8 entera y luego se promediaban los píxeles, así que apenas se
  ahorraba. Ahora sale un bloque de 4×4 o 2×2 usando solo los coeficientes de frecuencia baja, como `jidctred` de libjpeg:
  * solo enteros (vale también en el núcleo de arm64);
  * misma escala de continua: un color plano sale igual.

  En el anfitrión, con escala 4, es 2,4 veces más rápida. Se desvía de la foto entera promediada 1–2,5 niveles de 255 de media
  (0,3–0,9 más que antes): no se nota en una miniatura.

### 4 · Repintar agrupado

Mientras llegan imágenes, lo que solo cambia el contenido se repinta como mucho cada 250 ms (`PINTA_MIN_MS`, en `websvc.c`).
Desplazarse o cambiar el tamaño de la ventana se repinta al momento, como siempre. Al acabar las imágenes, se pinta lo último.

### 2 · La pasarela reutiliza las conexiones

En QEMU el HTTPS va por la pasarela del anfitrión (`tools/web_gateway.py`), que usaba `urllib`: una conexión TCP y un TLS
completos por cada recurso. Ahora (versión 5 de la pasarela):

* Se guardan hasta 6 conexiones abiertas por servidor durante 25 s y se reutilizan (HTTP/1.1 *keep-alive*).
* Si el servidor cierra una conexión guardada, la petición se repite con una nueva sin error.
* Un POST va siempre por una conexión propia: nunca se repite.
* Si hay un proxy configurado para esa URL, se sigue usando `urllib`, que lo respeta.
* La precarga en paralelo pasa de 8 a 12 descargas a la vez y de 90 a 160 recursos por página.
* `/status` cuenta las conexiones nuevas y las reutilizadas.

## Medido

| | Antes | Ahora |
|---|---|---|
| Portada de 120 fotos en QEMU (HTTP local, `webr3`) | 18,5 s, 122 fotogramas | **3,4 s**, 12 fotogramas |
| Memoria del navegador en esa portada | 54 MiB | 32 MiB |
| 120 fotos por la pasarela, una detrás de otra, con 50 ms de latencia hasta el servidor | 17,9 s (149 ms cada una) | **9,2 s** (77 ms) |

La reducción por conexión (de 149 a 77 ms) se suma a la precarga en paralelo, que ya existía y ahora también reutiliza conexiones.

## Pruebas

* `tests/test_jpeg_reducido_342.c` (en `make test`). Fotos sintéticas codificadas con el codificador de SOMA (calidad 60 y 90,
  color y gris):
  * error de lo reducido frente a la foto entera promediada;
  * un color plano sale igual;
  * muestra el tiempo antes y ahora.
* `tests/test_pasarela_342.py` (en `make test`, sin Internet: un servidor local hace de origen y cuenta conexiones):
  * 31 peticiones por una sola conexión;
  * el servidor que avisa de que cierra;
  * la conexión guardada que se corta sin avisar;
  * POST sin reutilizar;
  * respuestas por trozos;
  * 24 peticiones a la vez y el tope por servidor.
* `tests/prueba_navegador_341.py` (QEMU) sigue cargando las 120 fotos con 4 GiB y con 512 MB.
* `src/arch/x86_64/sbx_testimg.h`: cambia el resultado esperado de `jpeg_big_fit` (un JPEG de 640×480 decodificado a 160×120),
  porque la IDCT reducida da píxeles algo distintos; es el valor que calcula el decodificador nuevo (`test_sbx_ref.c --gen`).

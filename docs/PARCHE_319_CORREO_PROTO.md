# Parche 319 · Lo que manda el servidor de correo: un desbordamiento real, arreglado

**Hace falta `make`.**

## El problema
El núcleo lee las respuestas del servidor IMAP y SMTP con `correo_core.c` (literales `{N}`, `FETCH`, `LIST`, `SEARCH`, etiquetas,
`EHLO`). Esos lectores tenían pruebas de casos concretos, pero **ninguna con datos hostiles**. Un fuzzer nuevo
(`tests/test_correo_proto_319.c`: respuestas reales mutadas y cortadas, cada una en un búfer de su tamaño exacto, con ASan y
UBSan) encontró en segundos:

1. **Un desbordamiento de enteros explotable** en los literales IMAP (`valor`): con `{4294967295}` en medio de una línea,
   `i + v` daba la vuelta, el límite no saltaba y el cuerpo del mensaje quedaba con **4 294 967 295 bytes** dentro de una
   respuesta de 44. El núcleo copia después el cuerpo hasta su capacidad (`correo_red.c`): leía memoria del núcleo que no era
   suya (y la enseñaba como texto del correo, que puede acabar en el agente) o fallaba con un pánico. Basta con un servidor IMAP
   malicioso, o con suplantar al de verdad si la conexión no va cifrada.
2. **Índices que se pasaban 1 o 2 bytes** del final: una `\` al final de una cadena entre comillas, un `[` sin cerrar o un literal
   sin salto de línea; el dato devuelto era más largo que lo que quedaba del búfer.
3. **Comparaciones que no miraban la longitud** (`FETCH`, `UID`, `BODY[`, `NIL`, `LIST`, `SEARCH`, `OK`/`NO`/`BAD`, y las
   capacidades de `EHLO`): leían unos bytes de más al final del búfer. En el núcleo de hoy no se notaba porque el búfer acaba en
   NUL, pero las funciones reciben `(r, n)` y no lo respetaban.

## Qué cambia (`src/apps/correo_core.c`)
* `valor`: ningún índice pasa de `n`; el número de un literal se satura en lugar de dar la vuelta, y su longitud nunca pasa de
  lo que queda.
* `iempieza_n`, comparación acotada a la longitud, en todos los lectores de lo que manda el servidor.
* Las respuestas legítimas se leen igual (todas las pruebas de antes siguen pasando).

## Pruebas
* **Nueva** `tests/test_correo_proto_319.c` (en `make test`, 100 000 respuestas; probado con 400 000): sin ningún fallo de ASan ni
  UBSan con gcc 14 y clang, y el caso del literal enorme como regresión.
* Siguen pasando `test_correo_295` (154/154), `test_correo_red_295` (46/46 y 31/31: IMAP y SMTP con TLS 1.3 de verdad) y
  `test_correoapp_295` (81/81).

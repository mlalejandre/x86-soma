# Parche 201: escritura fuera del heap del nucleo con una respuesta HTTP, y fuzzing de HTTP y TLS

Requiere el parche 200.

## El fallo (seguridad, critico, remoto)

Cualquier servidor web (o cualquiera capaz de alterar una respuesta http sin cifrar) podia hacer que el nucleo copiase hasta
4 GiB mas alla de un bufer del heap. Basta esta respuesta:

    HTTP/1.1 200 OK
    Transfer-Encoding: chunked

    5

(la linea con el tamano del trozo, sin su salto de linea, y cierre de la conexion). En `dechunk()` (`src/apps/web_net.c`) el indice
pasaba del final de los datos y la resta `n - i` daba la vuelta a 0xFFFFFFFF; `wb_addn` aceptaba esa longitud porque su propia
comprobacion (`len + extra + 1 <= cap`) tambien daba la vuelta en 32 bits. El mismo codigo lo usan el navegador, el servicio de
red de ring 3 y, desde el parche 195, el cliente del LLM.

Comprobado en QEMU con un servidor que envia exactamente eso: antes, `acceso a memoria no valido` dentro del nucleo tras pisar el
heap; ahora la pagina simplemente llega vacia.

## El arreglo

- `dechunk()`: si la linea del tamano no termina, la respuesta se da por cortada; la suma de indice y tamano se hace en 64 bits.
- `wb_reserve()` (`web_util.h`, el bufer creciente que usa todo el motor): la cuenta se hace en 64 bits, ningun bufer pasa de
  2 GiB y el bucle de duplicar ya no puede quedarse dando vueltas. Es defensa en profundidad para todos sus usos, no solo este.

## Fuzzing nuevo

- `make fuzz-http` (`tests/fuzz_http_201.c`): el cliente HTTP del navegador contra un servidor simulado que devuelve respuestas
  generadas y mutadas (cabeceras rotas, Content-Length falso, trozos absurdos, gzip corrupto, redirecciones, cookies enormes) en
  fragmentos de tamano aleatorio; ademas analisis de URL y tarro de cookies. Encontro el fallo de arriba a las pocas miles de
  respuestas; despues del arreglo, 120.000 sin fallos. Una tirada corta (con los casos que rompian) queda dentro de `make test`.
- `make fuzz-tls` (`tests/fuzz_tls_201.c`): la maquina de estados del TLS 1.3 con la criptografia sustituida por funciones
  identidad, para que los mensajes "cifrados" lleguen al analizador tal cual: ServerHello, extensiones, certificados de hasta
  60 KiB, Finished, tiques, KeyUpdate, alertas y datos, con longitudes falsas y troceado aleatorio. 80.000 conexiones (una de
  cada cinco llega a establecerse) sin fallos.

Tambien se han revisado sin encontrar fallos: el analizador de respuestas DNS, el de opciones DHCP, el cliente HTTP minimo
(`http.c`) y, con 300.000 casos de fuzzing, los analizadores de respuestas del LLM y del lanzador (parches 195 y 196).

## Archivos

Cambiados: `src/apps/web_net.c`, `src/apps/web_util.h`, `Makefile`. Nuevos: `tests/fuzz_http_201.c`, `tests/fuzz_tls_201.c`.

# Parche 313 · El lector de páginas web ya no pierde párrafos

**Hace falta `make`.** Es el arreglo de un fallo de antes, del parche 288, que salió al preparar las pruebas del 312.

## El fallo
`wl_texto` (`src/agent/web_lee.c`) es lo que convierte una página web en el texto que lee el modelo en `investiga` y `search`.
Al final se quedaba con las líneas útiles y las compactaba **en el mismo búfer**, separadas por una línea en blanco (`\n\n`).
El comentario del código decía que la escritura nunca adelantaba a la lectura, pero no era así: en el original entre dos líneas
solo hay un `\n`. Con dos líneas útiles seguidas, la escritura iba un byte por delante y machacaba la línea siguiente antes de
copiarla, que se convertía en una ristra de saltos de línea.

En una página de artículo normal (título, párrafo y lista seguidos), el modelo recibía el título, cientos de líneas vacías y el
último párrafo: **se perdían el primer párrafo y la lista enteros**, justo lo importante. No se salía del búfer, pero el texto
llegaba mal.

## El arreglo
Dos pasadas sobre el mismo búfer:
1. se compacta con **un** salto entre líneas, que nunca puede adelantar a lo leído;
2. cada salto pasa a ser doble, **de derecha a izquierda**. Si no cabe, se quitan líneas enteras del final.

El resultado es el que se buscaba desde el principio: las líneas útiles separadas por una línea en blanco.

## Pruebas
* `tests/test_web_lee_313.c` (`make test`): la página del artículo da sus cinco líneas útiles (título, primer párrafo, dos
  elementos de la lista y último párrafo), nunca tres saltos seguidos, ni menú ni pie. 200 párrafos seguidos con búferes de
  50 a 9000 bytes: nunca se pasa, siempre termina en NUL, separador doble exacto y párrafos en orden. Con el código de antes
  fallan 6 de 12 comprobaciones.
* La prueba del 288 (`test_investiga_288`) sigue igual: 88/88.
* QEMU (`sbx-check`): la página de prueba del 312, leída a través del servicio aislado, trae el artículo entero.

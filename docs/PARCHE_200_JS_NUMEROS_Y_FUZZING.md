# Parche 200: tres desbordamientos de pila del nucleo desde JavaScript, y fuzzing del navegador

Requiere el parche 199.

## El fallo (seguridad, critico)

Cualquier pagina web podia escribir fuera de un bufer de 400 bytes en la pila del NUCLEO con una linea de JavaScript:

    (1e308).toString(2)                                        // escribe 1025 bytes
    (1).toExponential(100000)                                  // escribe los que pida la pagina
    (1.5).toLocaleString('en', {maximumFractionDigits: 1e5})   // idem

Las funciones que pasan un numero a texto (`web_f64.c`) no reciben el tamano del bufer de destino, y `Number.prototype` y
`toLocaleString` les daban 400 bytes sin acotar el numero de cifras. Con el protector de pila el resultado era un sistema
detenido (y desde el parche 198, la pagina cerrada); sin el, memoria del nucleo pisada con una longitud elegida por la pagina.

Lo encontro el fuzzing nuevo de este parche (el primero, a los pocos miles de programas); los otros dos, la revision del mismo patron.

## El arreglo

- `web_f64.h` declara `F64_STR_MAX` (1200) y `F64_DIGITS_MAX` (100). `f64_to_fixed`, `f64_to_exponential` y `f64_to_precision`
  acotan ellas mismas las cifras: ya no dependen de que quien llama valide.
- `Number.prototype.*` y `toLocaleString` usan buferes de `F64_STR_MAX`; `toExponential(d)` lanza `RangeError` fuera de 0..100
  (como manda el estandar) y `toLocaleString` acota `minimumFractionDigits`/`maximumFractionDigits`.
- `tests/test_f64_200.c` (en `make test`): 1.304 comprobaciones con buferes exactos bajo ASan; la salida mas larga posible es
  de 1.025 bytes.

Comprobado en QEMU con una pagina que hace las tres llamadas: antes, `[WEB] El motor del navegador ha fallado (desbordamiento
de pila detectado...)`; ahora la pagina termina su script con normalidad.

## Fuzzing del motor del navegador (nuevo)

El motor corre en ring 0 y hasta ahora solo se probaba con casos escritos a mano. Dos fuzzers nuevos, con ASan y UBSan:

- `make fuzz-web` (`tests/fuzz_web_200.c`): HTML y CSS generados y mutados -> analisis, estilos, maquetacion, pintado, consulta
  de punto, desplazamiento y segunda maquetacion con otro tamano. 26.000 casos sin fallos de memoria.
- `make fuzz-js` (`tests/fuzz_js_200.c`): programas armados con piezas que suelen romper motores (retrollamadas que cambian el
  array que se recorre, indices y longitudes extremos, vistas sobre bufferes, nodos que se quitan dentro de su manejador, nombres
  y cadenas largas en las API del DOM...). mas de 30.000 programas; el unico fallo de memoria es el de arriba.

`FUZZ_ITERS` y `FUZZ_SEED` cambian la tirada. Conviene dejarlos correr una noche con semillas nuevas de vez en cuando.
`tests/layhost/fbhost.c` (el framebuffer de pruebas) recorre ahora solo lo visible: con cajas de 99999999 px tardaba minutos.

Limite: en este entorno no estan las fuentes TTF, asi que el camino de texto con fuentes reales no se ha ejercitado.

## Archivos

Cambiados: `src/apps/web_f64.c/.h`, `src/apps/web_js_lib3.c`, `tests/layhost/fbhost.c`, `Makefile`.
Nuevos: `tests/test_f64_200.c`, `tests/fuzz_web_200.c`, `tests/fuzz_js_200.c`.

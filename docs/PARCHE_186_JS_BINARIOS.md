# Parche 186 — Motor JavaScript (III): datos binarios reales

Requiere el parche 185. Interruptor: `soma.jsbin=off` (y el modo seguro) devuelve el prelude anterior (arrays "disfrazados").

## Qué arregla

Los arrays tipados eran arrays normales con otro nombre: no compartían memoria entre vistas, no convertían los valores al asignar
(`a[0] = 300` en un `Uint8Array` guardaba 300), no existía `DataView` y `Blob`/`TextEncoder` trabajaban con cadenas, no con bytes.
Eso rompe cualquier código que decodifique imágenes, parsee formatos binarios, use WebSocket/fetch binarios o WebAssembly.

| Pieza | Antes | Ahora |
|---|---|---|
| `ArrayBuffer` | solo un número (`byteLength`) | bloque de bytes real; `slice`, `resize` (con `maxByteLength`), `transfer`, `detached` |
| `Uint8Array`… `Float64Array` (9 tipos) | arrays JS con otro nombre | vistas sobre un `ArrayBuffer` con `byteOffset`/`length`; las vistas comparten memoria |
| `a[0] = 300` en `Uint8Array` | 300 | 44 (conversión módulo 2^8; `Uint8Clamped` satura y redondea al par) |
| `Float32Array` / `Math.fround` | sin redondeo | redondeo IEEE al par más cercano, subnormales, ±Infinity |
| `DataView` | no existía | `get/set` Int8…Float64 con `littleEndian` |
| `TextEncoder` | `'é'` → 4 bytes | UTF-8 correcto; `encodeInto`; sustitutos sueltos → U+FFFD |
| `TextDecoder` | solo UTF-8 aproximado | UTF-8 (WHATWG: U+FFFD por subsecuencia máxima), UTF-16LE/BE, windows-1252/latin1, BOM, `fatal`, `stream` |
| `Blob`/`File` | cadena | `Uint8Array` interno; `size` en bytes, `slice` con índices negativos, `text`, `arrayBuffer`, `bytes` |
| `FileReader` | texto | `readAsText/DataURL/ArrayBuffer/BinaryString` sobre los bytes reales |
| `Response`/`Request`/XHR | cuerpo como cadena | `arrayBuffer()`, `blob()` y envíos binarios con los bytes exactos; `fetch('data:…;base64,…')` y `blob:` |
| `structuredClone` | JSON | copia de `ArrayBuffer`/vistas (nivel superior) |
| `Atomics` | no existía | versión de un solo hilo (load/store/add/sub/and/or/xor/exchange/compareExchange/notify/wait) |

## Diseño

* Nuevas clases del motor: `JC_ABUF` (`struct jbuf`: bytes, longitud, longitud máxima, indicadores), `JC_TYPED` y `JC_DVIEW` (`struct jta`: buffer,
  desplazamiento, longitud, tipo, longitud-seguida). Los bytes viven en el arena del script y cuentan contra su límite de memoria.
* El valor numérico del motor (`f64`) es el patrón de bits IEEE: `Float64Array` es un `memcpy`; `Float32Array` convierte con redondeo real.
* `js_get`/`js_getv`/`js_put`/`js_putv`/`js_has`, `for…in`, `Object.keys`, `js_iterate` (for-of, spread, `Array.from`) y los iteradores tienen
  una rama directa para `JC_TYPED`: leer o escribir un elemento es una lectura/escritura de memoria, sin objetos intermedios.
* `%TypedArray%.prototype` con 31 métodos nativos (`set`, `subarray`, `slice`, `fill`, `copyWithin`, `sort` estable por mezcla, `map`, `filter`,
  `reduce`, `find*`, `every`, `some`, `with`, `toSorted`, `toReversed`, `at`, `includes`, `join`…), accesores `buffer`/`byteLength`/`byteOffset`/`length`
  y `Symbol.toStringTag`. Funcionan las subclases (`class X extends Uint8Array`).
* Un `ArrayBuffer` no se puede pedir por encima de 16 MiB ni del límite del script: lanza `RangeError` (capturable), no detiene el script.
* Las vistas se comprueban contra la longitud real del buffer en cada acceso (buffers redimensionables o transferidos no dan lecturas fuera de límites).
* Piezas nativas para el prelude: `__utf8enc`, `__utf8into`, `__utf8dec`, `__rawbytes`, `__rawstr`, `__b64enc`, `__b64dec`.
* Con `soma.jsbin=off` no se registra nada de esto y el prelude conserva su comportamiento anterior.

## Pruebas

* `tests/test_web_js_186.c`: 139 casos de binarios + 10 del modo `off`, con ASan/UBSan.
* Los casos se contrastaron con Node (misma salida salvo los que dependen de su reino de objetos o de `FileReader`, que Node no tiene).
* Compilación x86_64 y aarch64 con `WERROR=1`.

## Límites conocidos

* `BigInt64Array`/`BigUint64Array` y `DataView.getBigInt64` llegan con BigInt (parche 189).
* Sin GC: los buffers no se liberan hasta cerrar la pestaña (tope de 24 MiB del script en total).
* `structuredClone` de objetos que contienen buffers dentro sigue pasando por JSON (el nivel superior sí se copia bien).
* Streams (`blob.stream()`, `response.body`) llegan en el parche 187.

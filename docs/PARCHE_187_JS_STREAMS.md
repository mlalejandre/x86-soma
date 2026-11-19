# Parche 187 — Motor JavaScript (IV): Streams, indexedDB, caches, WeakRef

Requiere el parche 186. Interruptor: `soma.jsstream=off` (en la línea `multiboot2 /boot/soma.elf ...` de GRUB).

## Qué añade

**Streams (WHATWG), escritos en el prelude**
- `ReadableStream` (cola con `highWaterMark` y `size`, `getReader()` y lector BYOB, `tee`, `pipeTo`, `pipeThrough`, `cancel`, iterador asíncrono `for await`, `ReadableStream.from`).
- `WritableStream` + `WritableStreamDefaultWriter` (`ready`, `desiredSize`, cola de escrituras, `abort`, `close`).
- `TransformStream` con contrapresión, `TextEncoderStream`, `TextDecoderStream` (acepta trozos UTF-8 partidos).
- `CountQueuingStrategy`, `ByteLengthQueuingStrategy`.
- `Response.body`, `Blob.prototype.stream()`, y `new Response(stream)` / `new Request(url, {body: stream})`. El cuerpo se consume una sola vez (`bodyUsed`) y leer `body` antes de `text()` lo marca como usado.

**indexedDB (en memoria; dura lo que la página)**
- `indexedDB.open/deleteDatabase/databases/cmp`, `onupgradeneeded` con `oldVersion/newVersion`, `VersionError`.
- Almacenes con `keyPath` (también anidados) o claves externas, `autoIncrement`, índices (`unique`, `multiEntry`), `IDBKeyRange`, cursores (`next/prev/nextunique/prevunique`, `continue/advance/update/delete`), `getAll/getAllKeys/count`.
- Transacciones `readonly/readwrite/versionchange`, `oncomplete/onabort/onerror`, `abort()` que revierte los cambios, eventos de error que burbujean de petición a transacción y a base de datos.
- Los valores se clonan con un algoritmo estructurado (Date, RegExp, Map, Set, ArrayBuffer, vistas, objetos y ciclos).

**CacheStorage (en memoria)**: `caches.open/has/delete/keys/match`, `Cache.put/add/addAll/match/matchAll/delete/keys`.

**Otros**: `WeakRef` y `FinalizationRegistry` (referencias fuertes: el motor no tiene GC, las finalizaciones nunca se disparan, lo que la especificación permite), `Symbol.prototype.description` y `Symbol.prototype.toString` correctos para símbolos de usuario, `Symbol.for` y bien conocidos.

## Interruptor y modo seguro
`soma.jsstream=off` define `__noStream` antes del prelude: no se instalan Streams, indexedDB ni caches (`typeof ReadableStream === "undefined"`). `Response.body` devuelve `null`, `Response.text()/arrayBuffer()` y `fetch` siguen funcionando como en el 186. `Symbol.description` y los cambios de cuerpo no dependen del interruptor.

## Límites conocidos
- indexedDB y caches no persisten entre cargas de página (el disco interno nunca se toca).
- Sin GC: `WeakRef.deref()` siempre devuelve el objeto.
- Transacciones: se ejecutan en orden por almacén, sin concurrencia real; `IDBTransaction.durability` es informativa.

## Cambios en el código
- `web_js_prelude.h`: bloque `SOMA_187` (todo envuelto en `if (!g.__noStream)`), `bodyTake`, getter `Response.body`, `Blob.stream`.
- `web_js_lib.c/lib2.c/run.c/i.h`: `js_sym_description`, `js_sym_tostring`.
- `web_js_bin.c`: `js_stream_on()` y `__noStream`.
- `tests/test_web_js_187.c`: 41 casos + 3 en modo off. Se compila con ASan/UBSan y `detect_stack_use_after_return=1`: con `=0`, ASan da falsos positivos al cambiar de pila de corrutina tras una excepción dentro de `await` dentro de `try` (issue 189 de sanitizers); con `=1` los marcos viven en el montón y el resultado es limpio.

## Verificación
`python3 parche_187_js_streams.py --plan | --self-test | --apply | --test | --undo`

# SOMA 145 — interoperabilidad de EventTarget y eventos DOM

Objetivo: mejorar interacciones JavaScript con cambios delimitados al prelude JS.
No incluye Blink, V8, nueva red, ni aislamiento del navegador Ring 3.

- Orden correcto de listeners `capture` antes de `bubble` sobre el propio objetivo.
- `stopImmediatePropagation()` corta los listeners restantes, incluido tras `onclick`.
- `removeEventListener()` evita la llamada si se retiró durante el despacho.
- `once` elimina el listener antes de invocarlo (también si hay despacho anidado).
- `AbortSignal.aborted` impide registrar; la señal retira listeners al abortar.
- El modo `passive` impide `preventDefault()` durante el listener.
- Tipos de evento arbitrarios (incluido `__proto__`) no heredan claves de Object.
- `onclick = () => false` cancela el evento cuando este es cancelable.
- Se rechaza el redisparo simultáneo del mismo objeto de evento.

Pruebas: `python3 parche_145_eventos.py --test` (requiere Node.js en el anfitrión).
Compilación de cabecera con clang/cc cuando está disponible. Las pruebas Node evalúan
el prelude real generado, **no** prueban el motor JS integrado ni la ejecución QEMU.

**Nota de implementación:** `src/apps/web_js_prelude.h` se declara como generado.
Si el proyecto genera de nuevo este archivo desde `tools/web_prelude.js`, deberá
integrar también estas modificaciones en la fuente de generación; de lo contrario
se perderán. No ejecutar regeneradores antes de consolidar la fuente.

Reversión: `python3 parche_145_eventos.py --undo`. La reversión verifica SHA-256
antes de modificar algo y no toca fuentes cambiados por el usuario.

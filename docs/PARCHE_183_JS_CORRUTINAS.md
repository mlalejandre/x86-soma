# Parche 183: Motor JavaScript (I) — generadores y async/await de verdad

Requiere el parche 182. El motor ejecutaba `await` de forma síncrona (bloqueando hasta resolver la promesa) y no
conocía `function*`. Las aplicaciones modernas (claude.ai, frameworks, `fetch` + `async`) dependen del orden real de
las microtareas y de generadores; ahora existen.

## Qué cambia

* **Generadores**: `function*`, métodos `*` en clases y literales de objeto, `yield`, `yield*`, `next/return/throw`,
  iteración con `for...of`, spread y desestructuración.
* **async/await real**: la función async se suspende en cada `await` y se reanuda desde la cola de microtareas, con el
  orden de ejecución de los navegadores (`sync end` se imprime antes de lo que sigue a un `await`).
* **Generadores asíncronos** y `for await (... of ...)`, con cola de peticiones.
* `try/catch/finally` correctos al suspender y al cerrar un generador con `return()` (los `finally` se ejecutan; los
  `catch` no capturan el desenrollado).
* Corrección: `var` y parámetros *rest* dentro de funciones flecha se escapaban al ámbito exterior (`var z=7` dentro de
  `()=>{}` dejaba `z` sin definir); afectaba a casi todo el código moderno.

## Cómo funciona

El intérprete recorre el árbol sobre la pila de C, así que cada generador/función async viva recibe su propia pila de C
(512 KB, reutilizadas desde un banco de 16), su propia cadena de excepciones y su propia pila de ámbitos; el cambio de
contexto (`web_js_co.c`, ensamblador x86_64) guarda solo los registros que exige el ABI. Las funciones async sin
`await` no usan pila propia. Si se agotan las pilas (tope 192 MB, p. ej. 400 `async` simultáneas) la función se ejecuta
en el modo antiguo (síncrono) en lugar de fallar.

## Interruptor y modo seguro

`soma.jsco=off` (o modo seguro): sin corrutinas; el motor se comporta como antes del parche (`await` síncrono, los
generadores no existen).

## Pruebas

`tests/test_web_js_183.c` (anfitrión, ASan/UBSan): 107 casos (generadores, orden de microtareas, `for await`,
generadores async, `try/finally`, `var` en flechas, `Promise.all`, 400 async concurrentes) contrastados con Node, más el
modo `off`. `python3 parche_183_js_corrutinas.py --test`. En QEMU, una página con generadores, `for await`, `Promise.all`
y `setTimeout` imprime el orden esperado.

## Límites

* Solo x86_64 (el T480s); en ARM el motor sigue como antes.
* Cada generador/async vivo con `await` cuesta 512 KB de pila (con el fallback síncrono al llegar al tope); la
  recursión dentro de una corrutina admite unos 300 niveles (fuera, 400).
* Sin recolector de basura (tope de arena de 24 MB).
* Siguen pendientes: cadenas UTF-16, `Map/Set` rápidos, `Proxy`, `BigInt`, módulos ES, `Symbol.description` y
  `AsyncFunction.name`.

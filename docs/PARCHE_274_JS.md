# Parche 274 · JavaScript como lenguaje de aplicaciones (orden `js`)

SOMA ya programaba en SOMA-LANG, Python (`python`) y C (`cc`). Este parche añade **JavaScript** con el mismo estilo:
el intérprete es el **motor del navegador** (`src/apps/web_js_*.c`: ES2015+, clases, async/await, generadores, BigInt, módulos CommonJS con
`require`) ejecutado como **proceso de ring 3 aislado** (`jsrun.elf`), sin página y sin red. HTML y proyectos web van en el parche 275.

## Uso

    js hola.js              ejecuta /workspace/hola.js (rutas relativas: /workspace)
    js -c "console.log(2**10)"
    js                      ayuda

Esc en el Terminal lo detiene (igual que `python` y `cc`). Tiempo máximo: 10 minutos.

## Qué hay dentro del programa

| Qué | Cómo |
|---|---|
| Salida | `console.log/info/warn/error`, `print(...)`; objetos y arrays formateados como en Node |
| Entrada | `input("pregunta")` (o `readLine`) → cadena, o `null` si no hay más entrada |
| Archivos | `fs.read(ruta)` (cadena o `null`), `fs.write(ruta, texto)` (true/false), `fs.exists(ruta)`. Escribir solo en `/workspace`; leer, lo que permite el VFS |
| Otros archivos | `require("./util")` (CommonJS: `module.exports`, `exports`, `__filename`, `__dirname`, caché, ciclos, `.json`). `import`/`export` de ES no: da un error con la pista |
| Tiempo | `sleep(ms)` (bloquea), `setTimeout/setInterval/clearTimeout`, `Date`, promesas, `async/await` |
| Salir | `exit(n)` → código de salida n (0..99) |
| Ventana | `win.create("título", ancho, alto)`, `win.active()`, `win.mouse()` → `{x,y,clicked,button,inside}`, `win.key()`, `win.size()`, `win.clear(c)`, `win.rect(x,y,w,h,c)`, `win.line(x1,y1,x2,y2,c)`, `win.circle(x,y,r,c)`, `win.pixel(x,y,c)`, `win.text(x,y,"t",c)`, `win.rgb(r,g,b)`, `win.present()`, `win.close()`, `win.UP/DOWN/LEFT/RIGHT`. Es la misma API que `soma.win` de Python y `win.*` de SOMA-LANG |
| Navegador | `document` existe (vacío) por si una librería lo toca; `fetch` falla ("Failed to fetch"): **no hay red** |

Bucle de juego típico:

    win.create("Pelota", 400, 300);
    let x = 0;
    while (win.active()) {
      win.clear(0x101820); win.circle(x % 400, 150, 20, 0xffcc00); win.present();
      x += 4; sleep(16);
    }

o con temporizadores: `function f(){ ...; win.present(); if (win.active()) setTimeout(f, 16) } f()`. La ventana se cierra sola al terminar el programa.

## Diferencias con el navegador (a propósito)

* **Sin el límite de 12 s por tarea** del navegador (un bucle de juego dura minutos); lo acota el tiempo máximo del proceso (10 min) y Esc.
* El programa corre con las capacidades de `python`: consola, reloj, ventana y VFS (escribir solo en `/workspace`). No tiene red, ni dispositivos, ni la memoria de nadie más.
* Memoria: una región de 160 MiB reservada bajo demanda; el recolector de basura del motor la reutiliza. Agotarla da `RangeError`, no un fallo.
* `Math.random` usa un generador xorshift sembrado con el reloj: **no es criptográfico** (`crypto.getRandomValues` tampoco debe usarse para secretos).

## Piezas

* `src/apps/jsrun_core.c/.h` — el núcleo (consola, `input`, `fs`, `win`, `require`, bucle de temporizadores). No toca el sistema: todo entra por `struct jsr_io`, por eso se prueba en el anfitrión.
* `src/user/jsrun.c` — el programa de ring 3: montón propio en `USER_HEAP_VA`, argumentos, E/S por `SYS_SOMA_CON`/`SYS_SOMA_VFS`, reloj.
* `src/agent/shell_js.c` — la orden `js` y `shell_js_source()` (para el Programador: comprueba `-s` o ejecuta `-x` código que aún no está en disco).
* Motor (cambios mínimos): `wjs_rt()` para registrar nativos y `task_ms` (límite de tarea configurable; el navegador sigue en 12 000 ms).
* Makefile: regla `jsrun.elf` (como `websvc.elf`, ~800 KiB), `jsprog.o` y el objetivo `pruebas-js` (lo llama `make test`).
* `lcm/manifiestos/js.json` — el LCM aprenderá «ejecuta el script de javascript juego.js» → `js juego.js` en el próximo reentrenamiento.

## Pruebas

* `tests/test_jsrun_274.c` (39): el núcleo con ASan/UBSan y E/S simulada (consola, input, fs, require, temporizadores, promesas, errores, exit, sintaxis, ventana).
* `tests/jsrun_host_274/run.sh` (24): el **programa de ring 3 real** (`jsrun.c`, montón, argumentos, bucle) con el motor compilado como en SOMA (con cambio de pila), ejecutado en Linux x86_64 con las llamadas al sistema emuladas: archivos, `-c`, `-s`, `-x`, `input`, errores, `exit`, recursión sin fin, memoria agotada, recolector con cientos de miles de objetos, ventana.
* `tests/test_js_manifiesto_274.py` (11): formato del manifiesto y que la orden está en la tabla.
* El motor sigue pasando sus pruebas (183, 185, 186, 187, 187a, gc_189; la 188 ya aborta en este entorno de pruebas por un problema de ASan con `sigaltstack`, igual sin el parche).

## Límites conocidos

* **No se ha podido arrancar en QEMU/hardware desde mi entorno**: lo que no se prueba en el anfitrión es la parte del núcleo (`js_proc`: región de memoria, captura de salida, Esc) y el cargador de ELF. Está copiado del patrón de `voz_run`/`cc_proc`.
* `import`/`export` de ES y `fetch` no están (usa `require`; sin red).
* El LCM aún no sabe pedir `js` en lenguaje natural: va en el reentrenamiento conjunto (cc + agenda + js + html).

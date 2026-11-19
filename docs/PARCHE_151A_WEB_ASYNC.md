# SOMA — Parche 151A: cargas de imágenes HTTP en E/S separada

## Diagnóstico

Antes, `wp_tick()` llama a `img_load_one()` y ese código puede permanecer
hasta 10 segundos esperando HTTP, bloqueando el mismo hilo que atiende eventos,
JavaScript y tareas de DOM. El parche 149 reduce repintados y el 150 optimiza
la rasterización, pero no esas esperas de red.

## Cambio experimental

- `SOMA_WEB_ASYNC_IMAGES=1` activa un hilo adicional de E/S (solo x86_64).
- Por defecto vale `0`: se conserva el comportamiento anterior.
- Un único recurso HTTP(S) se descarga de forma asíncrona. Se prioriza la imagen
  más cercana al viewport con la política de selección actual.
- El nuevo hilo NO accede a DOM, objetos de página ni decodificadores: solo a
  URL, referencia HTTP y cuerpo de la respuesta. El hilo de página decodifica.
- Protocolo SPSC con estados IDLE/QUEUED/RUNNING/READY y release/acquire.
- Generación de navegación, identidad de página, índice, hash y URL para no
  aplicar una respuesta vieja a un documento nuevo.
- Cancelación cooperativa durante conexión y recepción TCP. DNS y algunas
  partes de `tcp_send`/`tcp_close` pueden todavía demorar el descarte.
- Serialización del estado global de cookies, DNS y pasarela mediante mutex
  registrado antes de iniciar el hilo de imágenes.
- SVG en línea, `data:` y `file:` conservan la ruta síncrona original.
- No se admite combinar `SOMA_BROWSER_NET_RING3=1` con esta versión.

## Limitaciones

Esto NO vuelve asíncronas las descargas de HTML inicial, CSS ni scripts; las
operaciones JS `fetch()` siguen en su ruta antigua. Tampoco reduce por sí solo
el tiempo de decodificación, y puede haber contención por el mutex de red si
JavaScript hace una petición simultánea. La cancelación no es instantánea.

No se ha verificado todavía el arranque en QEMU, el rendimiento medido ni la
compatibilidad sobre hardware físico. **La opción es opt-in por seguridad.**

## Instalación

```bash
python3 parche_151_async_imagenes.py --plan
python3 parche_151_async_imagenes.py --self-test
python3 parche_151_async_imagenes.py --apply
python3 parche_151_async_imagenes.py --test
```

Recompila todo tras cambiar `EXTRA_CFLAGS`, pues `web_net.c` y `web_page.c`
necesitan el mismo valor del macro:

```bash
make clean
make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS=-DSOMA_WEB_ASYNC_IMAGES=1
```

Para volver al código original sin desinstalar el parche:

```bash
make clean
make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS=-DSOMA_WEB_ASYNC_IMAGES=0
```

`ejecutar.py` podría volver a compilar usando las opciones predeterminadas:
revisa sus argumentos antes de asumir que estás probando el modo asíncrono.

## Medición

Instrumentación opcional (`io_ms`, hasta recibir la respuesta en el hilo de
E/S, por puerto serie mediante `wp_log`):

```bash
make clean
make ARCH=x86_64 PLATFORM=qemu_virt 'EXTRA_CFLAGS=-DSOMA_WEB_ASYNC_IMAGES=1 -DSOMA_WEB_ASYNC_PERF_LOG=1'
```

Compara la MISMA página y resolución, con la misma configuración de QEMU:
latencia al desplazarse con imágenes descargándose, clics durante HTTP lento,
tiempo hasta primera imagen, número de fallos y uso de memoria. Preferir
métricas p95/p99 del retardo de eventos y no únicamente FPS medios.

## Reversión

```bash
python3 parche_151_async_imagenes.py --undo
make clean
```

El script conserva una copia de cada fuente con SHA-256 y no revierte si
hay modificaciones posteriores. No cambia el kernel, Makefile ni discos.

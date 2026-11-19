# Parche 152 — Navegador fluido

Objetivo: que el navegador de SOMA responda como Chromium/Chrome en lo que más se nota: la rueda y el teclado mueven la
página al instante y con animación suave, el escritorio no le roba la CPU, pasar el ratón no rehace la página entera y
las páginas no se “estropean” al cabo de un minuto.

Todo se midió arrancando SOMA en QEMU (x86_64, TCG sin KVM, 1 CPU, 1920×1080) con páginas de prueba servidas desde el
anfitrión: una página larga de texto y un portal con Bootstrap 5.3 (230 KB de CSS), 60 tarjetas con foto y un reloj en JS.

## Qué estaba mal (medido)

| Problema | Medida antes |
|---|---|
| Con cualquier ventana abierta, el latido de 30 Hz borraba y recomponía la pantalla entera (fondo animado, sombras, marcos y el cuerpo de cada ventana) y la volcaba completa | 22 ms de cada 33 ms: **~70-79 % de la CPU** con la página quieta |
| La rueda solo cambiaba `sy`; el dibujo esperaba al siguiente latido | hasta 33 ms de espera + 13-21 ms por fotograma |
| Cada paso de rueda: relleno blanco de toda la vista + copia de la instantánea + relectura de toda la vista (bucle `volatile` píxel a píxel) | 3 copias de ~4,5 MB por paso |
| Al desplazar, el elemento bajo el ratón cambia; con reglas `:hover` (cualquier web con Bootstrap) eso recalculaba estilos y maquetaba **todo** el documento… durante el desplazamiento | 9 maquetaciones completas cada 3 s mientras se desplaza |
| La clave de la instantánea incluía `p->hover`: cada cambio de hover forzaba un repintado completo (que no cambia nada) | 2 fotogramas por paso de rueda |
| Tras cargar, dos maquetaciones completas más aunque los scripts no hubieran tocado nada | +2 × (estilos + cajas) |
| `var()` recorría la lista enlazada de ~300 variables de `:root` comparando cadenas | ~25 % del cálculo de estilos |
| Cada recálculo de estilos volvía a crear todas las variables CSS en el arena del documento (que no se libera hasta salir de la página); `querySelector`/`matches` recompilaban el selector en ese mismo arena | ~290 KB por recálculo: con un reloj en JS o al pasar el ratón el arena (16 MB) se agotaba en ~1 minuto y **la página perdía los estilos** (tarjetas sin bordes, botones sin color) |
| Cada repintado completo volvía a escalar píxel a píxel todas las fotos visibles | 25-57 ms por repintado |

## Qué cambia

1. **Compositor por demanda** (`src/ui/gui.c`). Si la ventana de arriba es el navegador, se repinta *solo esa ventana* y
   *solo cuando su huella cambia* (`browser_frame_dirty`), hasta 100 fotogramas/s. El fondo animado baja a 4 fotogramas/s
   y se congela del todo mientras se usa el navegador o la página carga (`browser_hot`), igual que un compositor deja de
   recomponer capas ocultas. Mover, redimensionar, abrir/cerrar ventanas, el dock o los ajustes recomponen todo al instante.
2. **Instantánea en anillo** (`browser.c`, `web_fluido_152.h`). La fila *i* de la vista vive en la fila `(r0+i) % h` del
   buffer: desplazar es mover `r0` y pintar solo la franja nueva (una copia en vez de tres). Sin rellenos redundantes.
3. **Desplazamiento suave** como Chrome/Firefox: la rueda (80 px por muesca) y las teclas (flechas, AvPág/RePág, espacio,
   Inicio, Fin) fijan un objetivo; cada fotograma avanza una fracción de lo que falta (~100 ms por muesca, independiente
   de los fotogramas por segundo). Si otra cosa mueve la página (anclas, barra, JS) la animación se cancela.
4. **Hover aplazado durante el desplazamiento** (como Chromium) y **invalidación selectiva de `:hover`**
   (`wc_hover_affects`): solo se reestila si algún compuesto con `:hover` puede encajar en un nodo cuyo estado cambió.
5. **Estilos más rápidos** (`web_css.c`): filtro de Bloom de clases por elemento (descarta reglas candidatas y ancestros
   sin buscar el atributo `class`), caché de `var()`, y se quitan las dos maquetaciones extra tras los scripts si el DOM no
   cambió. Resultados de estilo idénticos bit a bit a los anteriores (comprobado en las páginas de prueba).
6. **Sin fugas en el arena del documento**: variables CSS internadas (un nodo idéntico se reutiliza) y caché real de
   selectores. Arreglado el fallo de “la página se queda sin estilos al cabo de un rato”.
7. **Caché de imágenes escaladas** (`web_layout.c`): cada foto se escala una vez por tamaño/radio; los repintados copian.
   Mismos píxeles que antes (comprobado píxel a píxel contra la build original).
8. **Compilar en macOS** (`Makefile`): el `gcc` de macOS es Apple clang para el propio Mac y no acepta `-march=x86-64`.
   Ahora se detecta y se usa el toolchain cruzado de Homebrew si está, o se explica qué hacer; y `make docker-make`
   compila en el contenedor `soma-toolchain` pasando `EXTRA_CFLAGS`.
9. Arreglada la advertencia de `web_js_dom.c` (sangría engañosa) que rompía `make WERROR=1`/CI.

## Resultados (misma máquina, QEMU TCG, antes → después)

| Medida | Antes | Después |
|---|---|---|
| CPU de QEMU con la página quieta (página larga) | 71 % | 18-19 % |
| CPU de QEMU con la página quieta (Bootstrap + reloj JS) | 79 % | 27 % |
| Fotogramas mientras se desplaza | ≤ 30/s, 1 por muesca | 60-95/s, animados |
| Coste de un fotograma de desplazamiento | 13-21 ms (+ hasta 33 ms de espera) | ~5,5 ms (máx. 9-15 ms) |
| Maquetaciones por hover mientras se desplaza | 9 cada 3 s | 0 |
| Cálculo de estilos (Bootstrap) | 68-76 ms | 37-48 ms |
| Carga completa del portal Bootstrap | 309-330 ms | ~229 ms |
| Repintado completo de la vista | 25-57 ms | 15-25 ms |
| Arena del documento por recálculo | ~290 KB (se agotaba) | 0 |

## Opciones de compilación (todas por defecto activas salvo la de registro)

| Macro | Defecto | Efecto |
|---|---|---|
| `SOMA_WEB_FLUIDO` | 1 | 0 = compositor como antes (latido completo a 30 Hz) |
| `SOMA_GUI_BG_MS` | 250 | periodo del fondo animado con el navegador delante (33 = 30 fps como antes) |
| `SOMA_WEB_FRAME_MS` | 10 | separación mínima entre fotogramas del navegador |
| `SOMA_WEB_SMOOTH_SCROLL` | 1 | 0 = saltos instantáneos |
| `SOMA_WEB_WHEEL_PX` | 80 | píxeles por muesca de rueda (antes 48) |
| `SOMA_WEB_IMG_CACHE_KB` | 12288 | presupuesto de la caché de imágenes escaladas |
| `SOMA_WEB_PERF_LOG` | 0 | 1 = líneas `[web152]` por el puerto serie (carga, maquetación, fotogramas cada 3 s) |

## Cómo medirlo en tu máquina

```sh
make clean
make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_ASYNC_IMAGES=1 -DSOMA_WEB_PERF_LOG=1'
python3 tools/web_bench.py --serve ./carpeta_con_paginas http://10.0.2.2:8000/pagina.html
```

También se puede arrancar QEMU a mano con el navegador ya abierto en una URL:
`-fw_cfg name=opt/soma/openurl,string=http://10.0.2.2:8000/pagina.html`.

## Pendiente (visto durante las pruebas, fuera de este parche)

- **jQuery 3.7**: una página que carga `jquery.min.js` queda en blanco y los scripts posteriores no se ejecutan, sin
  error en el registro. Ya pasaba antes del parche. Es de compatibilidad del motor JS, no de rendimiento.
- El reestilado tras un cambio real de `:hover` sigue siendo de documento completo (Chrome solo reestila los nodos
  afectados y no maqueta si solo cambian colores/sombras). Es el siguiente paso natural.
- Las hojas de estilo externas se descargan una detrás de otra.

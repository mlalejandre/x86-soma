# SOMA 149 — Desplazamiento incremental y latencia de hover

## Problema
Antes, cada variacion de `sy` invalida el cache de framebuffer de la pagina:
se repintan todas las cajas, textos e imagenes. El raton tambien hace
`wl_hit()` de forma sincrona en el hilo de GUI durante la rueda.

## Solucion (x86_64 y AArch64)
- En desplazamientos menores que la altura del viewport se copia la parte
  reutilizable de la ultima instantanea y se redibuja **solo** la franja nueva.
- Se usa un plan puro (`web_scroll_149.h`) con aritmetica de 64 bits;
  previene desbordamientos al calcular deltas.
- La optimizacion exige cache valido, mismo contenido (huella excluyendo sy),
  mismas dimensiones/ubicacion, ninguna maquetacion pendiente y **ningun**
  elemento `position:fixed` / `position:sticky`. Con contenido complejo se
  vuelve al repintado completo existente.
- La rueda evita `wl_hit` en la GUI durante el desplazamiento. Los hover
  de movimientos proximos se limitan a 1 cada 24 ms; el hilo web sigue
  recibiendo el movimiento del raton para actualizar CSS y eventos.

## Interruptor de seguridad
La optimizacion esta activada por defecto en esta version. Para comparar
el comportamiento anterior, compilar todo el proyecto con:

```
make clean
make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS=-DSOMA_WEB_FAST_SCROLL=0
```

No se modifican los datos de usuario ni las imagenes de disco.
`browser.c` se enlaza en la imagen del kernel: recompilar SOMA SI cambia su binario.
Para volver a habilitarla, recompilar sin la opcion. Los archivos se pueden revertir
con `python3 parche_149_scroll.py --undo` (rechaza cambios posteriores).

## Verificacion
```
python3 parche_149_scroll.py --plan
python3 parche_149_scroll.py --self-test
python3 parche_149_scroll.py --apply
python3 parche_149_scroll.py --test
python3 ejecutar.py -t x86_64
```

En QEMU comparar una pagina larga con muchas imagenes, una pagina con navbar
`position:sticky` y otra con animaciones/DOM dinamico. Confirmar desplazamiento,
texto, hipervinculos, formularios y cambio de pestanas.

## Limitaciones
- No acelera por si solo el tiempo de descarga de imagenes o JavaScript, porque
  estos siguen siendo trabajos del motor web; el objetivo directo es evitar
  trabajo extra de rasterizado y busquedas de hover en cada evento de rueda.
- El rendimiento real depende del coste del framebuffer y el tipo de pagina.
- Esta optimizacion no es aislamiento del navegador en Ring 3.
- Validar en QEMU/placa antes de usar en el sistema principal.
- Diagnostico opcional: `EXTRA_CFLAGS=-DSOMA_WEB_PERF_LOG=1` informa
  por consola serie de repintados de 30 ms o mas, como maximo cada 5 s,
  junto con la ultima duracion de layout e imagenes pendientes.

# Parche 153 — No dibujar el orbe donde lo tapa una ventana opaca

Se aplica encima de los parches 152 a 152D.

## Medición previa

Cada fotograma del escritorio se repartía así, con el navegador abierto y el fondo a 30 fps (QEMU TCG):

| Parte | % del fotograma |
|---|---|
| Cintas del orbe (`ofx_draw_ribbons`) | 38 % |
| Halo del orbe | 7 % |
| Cuerpo del navegador | 16 % |
| Sombra y marco de la ventana | 14 % |
| Volcado a la pantalla | 14 % |
| Borrado de la pantalla | 9 % |

Primero se probó a reutilizar los píxeles de la ventana del navegador en vez de redibujarla. La comparación alternada
dio una ganancia del 2-3 %, dentro del ruido de la medida: el cuerpo del navegador ya era casi todo una copia de
píxeles (su instantánea), y reutilizarlo cambia una copia por otra del mismo tamaño. Ese cambio se descartó.

## Qué hace este parche

El orbe (cintas y halo) se dibuja píxel a píxel, también **debajo de la ventana**. Si la ventana de arriba es
**opaca**, esos píxeles se sobrescriben después y nunca llegan a verse. Ahora no se calculan:

- Solo cuando la ventana de arriba es opaca: opacidad al 100 %, o "ventana activa opaca" en los ajustes. Con
  transparencia se dibuja todo como antes, porque el fondo se ve a través de la ventana.
- El rectángulo oculto se recorta en el radio de las esquinas redondeadas más 2 px, para que por las esquinas se siga
  viendo el fondo.
- Solo dentro del fotograma completo (`her_refresh_all`), donde las ventanas se dibujan justo después.
- El aspecto no cambia: lo que se omite es exactamente lo que la ventana tapa.

## Resultado (comparación alternada en el mismo arranque)

```
fotograma del escritorio SIN ocultar el orbe:     55.9 Mciclos  (5 tandas de 6 s)
fotograma del escritorio CON ocultar el orbe:     37.5 Mciclos  (6 tandas de 6 s)
ahorro: 33 %
```

Con esto, el orbe a 15 fps (`-DSOMA_GUI_BG_MS=66`) cuesta lo mismo que antes a unos 10 fps.

## Cómo comparar en tu máquina

```sh
make clean && make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_ASYNC_IMAGES=1 -DSOMA_WEB_PERF_LOG=1 -DSOMA_GUI_BG_MS=33'
python3 tools/desk_bench.py --seconds 75
```

Con `-DSOMA_WEB_PERF_LOG=1` la build alterna sola los dos modos cada 6 s. La herramienta da la media de cada uno y el
ahorro. Para el uso normal, compila sin `SOMA_WEB_PERF_LOG`: la ocultación queda siempre activa. Con
`-DSOMA_ORB_OCCLUSION=0` se desactiva.

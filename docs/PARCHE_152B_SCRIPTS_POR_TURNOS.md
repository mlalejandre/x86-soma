# Parche 152B — Scripts por turnos e imágenes sin remaquetar

Se aplica encima del 152. Viene de medir la portada de 20minutos.es en el Mac del usuario:

```
red=1507 html=47 estilos+maquetacion=326 scripts+onload=2193 total=4073 laydur=234
```

Más de la mitad de la carga eran scripts, y mientras se ejecutaban la pestaña no atendía clics ni teclas.

## 1. Scripts por turnos

Antes, la navegación ejecutaba todos los scripts de golpe dentro de la carga. Mientras tanto, el hilo del navegador no
procesaba clics, teclas, hover, temporizadores ni imágenes. Ahora:

- La página se maqueta y se muestra en cuanto llegan el HTML y el CSS (`visible_en=` en el registro).
- Los scripts se ejecutan en el mismo orden, en turnos de unos 25 ms desde `wp_tick`. Entre turnos se atienden los
  clics y las teclas, y las imágenes siguen descargándose (en paralelo si `SOMA_WEB_ASYNC_IMAGES=1`).
- Cuando termina el último script: maquetación si algo cambió, eventos `DOMContentLoaded`/`load` y el estado
  "Listo (x s)".
- Durante esa fase no se disparan temporizadores ni se remaqueta. Así se mantiene el orden de antes y la página no se
  maqueta una vez por cada script.

Prueba con 8 scripts externos de 400 ms cada uno: la página es visible a los 0,18 s y responde a las teclas en unos
0,27 s. Antes no respondía hasta el final, a los 4 s.

Con `-DSOMA_WEB_SCRIPTS_TICK=0` se vuelve al comportamiento anterior.

Con `-DSOMA_WEB_PERF_LOG=1`, cada script que tarda 20 ms o más aparece como `[web152] script <ms> <url>`, y al final
sale `[web152] scripts terminados: total=<ms>`. Sirve para ver qué scripts de una web real cuestan más.

## 2. Imágenes sin remaquetar

Cuando llega una foto, solo se vuelve a maquetar si la maquetación dependía de su tamaño natural. Las imágenes con
ancho y alto ya fijados (atributos `width`/`height` o CSS), y las de fondo, solo se repintan. Así desaparecen las
maquetaciones de cientos de ms y los tirones al desplazar mientras carga una portada. El resultado es idéntico píxel a
píxel.

## Pendiente: red en paralelo

Todos los scripts y hojas se descargan uno detrás de otro: `http_exchange` mantiene `net_client_lock` durante toda la
petición. Hacerlo en paralelo es el siguiente gran ahorro en webs con muchos scripts, pero toca la pila TCP y la
pasarela HTTPS. Hay que probarlo con tráfico real.

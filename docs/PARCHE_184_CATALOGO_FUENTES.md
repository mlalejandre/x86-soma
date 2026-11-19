# Parche 184: Catálogo general de fuentes — las del navegador, también en el sistema

Requiere el parche 183. Las fuentes que usa el navegador (DejaVu Sans y Liberation Sans regular/negrita, parche 182) vivían
aparte: solo se podían usar dentro de las páginas web. Ahora son caras TrueType del **catálogo general** del sistema y se
pueden elegir para el escritorio, para las aplicaciones y los documentos, y para el editor de texto. Sigue habiendo **una
sola copia** de cada fuente en el kernel (los bytes viven en `web_font.o` y el catálogo los reutiliza).

## Qué cambia

* **Catálogo (`font`)**: tres entradas nuevas, 36 en total: `dejavu-sans` (33), `liberation-sans` (34), `liberation-sans-bold`
  (35). `font liberation-sans` cambia la fuente del escritorio (rótulos, iconos, ventanas, texto en rejilla de 8 px). Si una
  cara no tiene un glifo (flechas, símbolos), sale de DejaVu Sans o de la mono. La vectorial de siempre (`vector`) es DejaVu
  Sans Mono y conserva su número (32).
* **Ajustes > Fuentes** (página nueva, icono "Aa"):
  * *Fuente del escritorio*: DejaVu Mono, DejaVu Sans, Liberation, Liberation negrita, con una muestra. El catálogo completo
    (incluidas las de mapa de bits) sigue en el comando `font`.
  * *Interfaz y documentos*: DejaVu Sans, **Liberation Sans** (nueva) o DejaVu Sans Mono, más el tamaño (90–120 %). Afecta al texto
    proporcional de las aplicaciones y del visor de documentos; no a las páginas web. (Esta tarjeta estaba en Apariencia; se ha movido aquí.)
  * *Editor de texto (Notas)*: DejaVu Sans Mono, DejaVu Sans o Liberation Sans. El terminal sigue siempre con DejaVu Sans Mono.
  * *Restablecer* vuelve a los valores de siempre.
* Se guarda en `/etc/ui.conf` (`familia=`, `editor=`, `escritorio=`; `fuente=sans|mono` se sigue escribiendo y leyendo para
  ser compatible con versiones anteriores).
* El texto proporcional de la interfaz con Liberation usa las mismas caras y anchos que el navegador (anchos de Arial) y no
  depende de `soma.webfont`.

## Interruptor y modo seguro

`soma.sysfont=off` (o modo seguro): el catálogo vuelve a las 33 fuentes de antes (las nuevas no aparecen ni se pueden activar), la
interfaz con Liberation vuelve a DejaVu Sans, el editor a la mono y la página Fuentes lo indica. `soma.webfont=off` sigue
afectando solo a las páginas web.

## Pruebas

`tests/test_font_184.c` (anfitrión, ASan/UBSan, 87 comprobaciones): 36 entradas y orden alfabético, `font_set*`, interruptor
`off`, que cada cara dibuja distinto y la caché de glifos no las mezcla, que el terminal sigue con la mono, el respaldo de
glifos, y los anchos de la familia Liberation de la interfaz (20279 / 21925 en 1/64 px a 16 px, de las tablas `hmtx`),
independientes de `soma.webfont`. `python3 parche_184_catalogo_fuentes.py --test`.

## Límites

* Las fuentes del escritorio y del editor van en **rejilla de 8 px** (como la mono): una fuente proporcional queda algo más
  espaciada que en el navegador. El texto proporcional de verdad está en la interfaz y los documentos.
* Aún no hay serif, cursiva real, *kerning* ni CJK; `font-family` de las páginas web no elige entre las del catálogo.
* El catálogo no se amplía con fuentes del usuario (cargar `.ttf` desde disco queda para otro parche).

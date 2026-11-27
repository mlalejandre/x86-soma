# Parche 351 · Cálculo de estilos 2-3 veces más rápido

**Hace falta `make`.** Requiere el 350.

## Qué se ha medido

Con el registro de tiempos del navegador (`-DSOMA_WEB_PERF_LOG=1`) en QEMU, en la portada de 20minutos, cada vez que la página
se vuelve a maquetar se pasaban unos **200 ms calculando estilos** y solo unos 25 ms colocando cajas. Los scripts de la página
provocan una maquetación por segundo, más o menos, así que el cálculo de estilos se llevaba buena parte de la CPU del navegador
durante y después de la carga.

El perfil en el anfitrión (gprof, la misma página) mostraba el porqué: cerca de **1,3 millones de comprobaciones de selector por
pasada** para unos 4000 elementos. Había dos causas:

1. **Cubos compartidos.** Las reglas se reparten por su clase o id más a la derecha en una tabla de 128 cubos. Con miles de clases
   (PyPI, 20minutos), cada cubo mezclaba muchas, y el 94 % de las reglas candidatas de clase fallaban en el propio selector.
2. **Antepasados.** En `.menu .item a`, cada `a` subía por toda la cadena de padres buscando `.item` y luego `.menu`, aunque la
   página no tuviera ningún `.menu`. En la lista por etiqueta, la mayoría de las candidatas fallaban ahí.

## Qué cambia (`src/apps/web_css.c`)

* **1024 cubos** para clases e ids, y cada regla guarda el hash completo de su clave. Una regla de otra clase que cae en el mismo
  cubo se descarta comparando un número, sin mirar el selector. Si un elemento repite una clase (`class="a a"`), su cubo se
  recorre una sola vez.
* **Filtro de antepasados**, la técnica de WebKit. Mientras se calculan los estilos, un filtro de Bloom con contadores (4096
  entradas) lleva las etiquetas, ids y clases de la cadena de padres del elemento. Las partes del selector unidas por un espacio o
  `>` tienen que estar en esa cadena. Si falta alguna, la regla no puede coincidir y no se recorre la cadena. Las partes tras `+`
  o `~` son hermanos y no cuentan.
  * El filtro solo da falsos positivos, y entonces decide la comprobación de siempre.
  * En la cadena entra todo lo que la comprobación puede visitar subiendo por los padres, incluidas las raíces sombra. Un hijo de
    la sombra con `.host > .s` llega al anfitrión.
  * La pila del filtro admite 2048 niveles. En un árbol más hondo, desde ahí se calcula sin filtro, como antes. Sin memoria
    también.

## Resultados

| | antes | después |
|---|---|---|
| 20minutos, QEMU, estilos por maquetación | ~200 ms | ~75 ms |
| 20minutos, anfitrión `-O2` | 16,4 ms | 8,3 ms |
| PyPI (`/project/requests/`), anfitrión `-O2` | 16,5 ms | 10,2 ms |
| comprobaciones de selector simple por pasada (20minutos) | 1,3 M | 0,11 M |

El resultado es **el mismo**: el volcado de todas las cajas (posición y tamaño de cada elemento) de 20minutos y PyPI, a 1253 y
600 px de ancho, es idéntico byte a byte antes y después.

## Prueba nueva: `tests/test_web_estilos_351.c` (en `make ci`)

Cada documento se calcula dos veces, sin el filtro (`g_wc351_off = 1`) y con él. Se compara, elemento a elemento, el color, el
tamaño de letra, el `display` y el margen. Los casos son:

* descendiente, hijo directo, hermano seguido de descendiente, hermano general;
* id y etiqueta antepasados, y antepasado ausente;
* clases repetidas;
* un árbol de 2300 niveles (más hondo que la pila del filtro);
* 3000 clases en la misma tabla;
* raíz sombra con `>` hasta el anfitrión;
* 300 documentos y hojas de estilo generados al azar, con los cuatro combinadores, `:first-child` y `:not()`.

Para comprobar que la prueba detecta errores, se metió a propósito uno en el filtro (contar los hermanos como antepasados): falla
en 303 de las 316 comprobaciones.

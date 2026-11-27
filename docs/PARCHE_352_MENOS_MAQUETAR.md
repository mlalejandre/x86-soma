# Parche 352 · Menos maquetaciones, hojas de estilo en paralelo y JavaScript que se analiza 4 veces más rápido

**Hace falta `make`.** Requiere el 351.

## 1 · El JavaScript solo hace maquetar cuando algo visible cambia

Cada cambio del DOM desde JavaScript avisaba a la página para que recalculara todos los estilos y maquetara otra vez. Medido en
QEMU en la portada de 20minutos durante un minuto, con el registro de tiempos del navegador: unas **63 maquetaciones**. Casi todas
venían de un carrusel (`<ul>`) que cada pocos segundos hace dos cosas:

* Vuelve a escribir `style.transition` con el mismo valor que ya tenía. Esa escritura no cambia nada.
* Cambia `style.transform`. Esa sí mueve el carrusel.

Ahora no se pide maquetar cuando:

* **El valor no cambia.** Esto vale para `style.x = …`, `style.setProperty`, `style.removeProperty` de algo que no estaba,
  `className`, `id`, `classList`, `setAttribute` y `removeAttribute`, `dataset`, `hidden`, los atributos reflejados (`src`,
  `title`, `width`…) y el texto de un nodo de texto igual al que tenía (un reloj que no ha cambiado).
* **El nodo no está en el documento.** Por ejemplo, `new Image().src = …` de un contador de visitas, o un `<div>` suelto que se
  usa de plantilla con `innerHTML`. Al meterlo en el documento, `appendChild` e `insertBefore` avisan como siempre.

Lo demás no cambia:

* Los `MutationObserver` reciben los mismos registros que antes.
* La carga de imágenes desde JavaScript va por su propio camino.
* `value`, `checked` y `disabled` siguen avisando siempre, porque afectan al estado del control.

Resultado en 20minutos: de ~63 a **~38 maquetaciones por minuto**. Las que quedan son el carrusel moviéndose de verdad.

### `style` siempre con el mismo formato

`wc_set_inline` dejaba un espacio final según el orden de las escrituras. El atributo cambiaba aunque el valor fuera el mismo, y la
página se volvía a maquetar. Ahora el atributo queda siempre como lo escribe Chrome: `color: blue; top: 1px;`.

## 2 · Hojas de estilo en paralelo

Las hojas enlazadas (`<link rel="stylesheet">`) se bajaban una detrás de otra antes de poder dibujar nada. Ahora, al empezar a
leer los estilos, se piden todas a la vez, cuatro como mucho, en las corrutinas de descarga del parche 343. Se añaden **en el
orden del documento**, que es el de la cascada:

* Una hoja que llega antes que la anterior espera su turno.
* Mientras se espera una, avanzan las demás.
* Las que no caben en los cuatro huecos se piden al quedar uno libre.

Si la página se para, la descarga que quede se libera sola al terminar. Fuera del proceso aislado del navegador (y en arm64) todo
sigue igual que antes.

Medido en QEMU con un servidor local que tarda 300 ms en cada hoja y una página con 8 hojas:

| | antes | después |
|---|---|---|
| peticiones de las hojas | una tras otra (2,4 s) | dos tandas de 4 |
| «Listo» en la barra de estado | 2,9 s | 0,8 s |

Orden comprobado: una hoja lenta (1,2 s) seguida de una `<style>` y de otra hoja rápida que la pisa. La rápida llega antes pero
se aplica después, como en Chrome.

## 3 · Analizador de JavaScript

El SDK de consentimiento de 20minutos (Didomi, 513 KB minificados) tardaba unos 3 s en QEMU. En el anfitrión con callgrind:

1. **Intentos de flecha.** Ante cada `(` el analizador prueba si empieza una función flecha `(a, b) => …` y, si no lo es, vuelve
   atrás. Al fallar, formateaba el mensaje de error con su columna, y para eso recorría hacia atrás hasta el principio de la línea.
   En un archivo minificado la línea es el archivo entero. El mensaje se tiraba a continuación. Era el 86 % del análisis. Ahora,
   durante un intento, el error no se formatea. Los errores de verdad conservan la columna y el fragmento, también dentro de un
   `(` que se probó como flecha.
2. **Operadores.** Cada símbolo se comparaba, con `strlen` y `memcmp`, con los 58 operadores de la tabla: 7,8 millones de
   `memcmp`. Ahora solo se miran los que empiezan por el mismo carácter, en el mismo orden (los largos primero).
3. **Palabras reservadas.** Cada nombre se comparaba con las 38. Ahora solo si mide de 2 a 10 letras y empieza entre la b y la y
   (como todas), y solo con las de su misma inicial.

El resultado del análisis es el mismo. Medido en el anfitrión (`-O2`, sin otra carga):

| script | antes | después |
|---|---|---|
| SDK de consentimiento (513 KB) | 185 ms | ~45 ms |
| `loader.js` (207 KB) | 18 ms | 12 ms |
| `global.bundle.js` (113 KB) | 19 ms | 6 ms |
| instrucciones para el SDK (callgrind) | 843 M | 397 M |

## 4 · Límite de sockets de los procesos aislados (fallo del 345)

Al verificar este parche, `make sbx-check` falló una vez. Un proceso de prueba sin privilegios abrió 6 sockets cuando el límite es
4. La causa estaba en `sandbox.c` (tres sitios, uno de ellos el que lanza netsvc) y en `vplay_sys.c`: la estructura
`uexec_spec` se declaraba en la pila sin ponerla a cero. El campo `max_sockets`, que añadió el parche 345, quedaba con lo que
hubiera en la pila. Si era un número positivo, el proceso recibía hasta 8 sockets (el límite del navegador) en vez de 4. Ahora la
estructura se pone entera a cero, como ya se hacía en `usermode.c`, `ia_sys.c` y `web_r3.c`. La prueba que lo detectó es la de
siempre (`sbx-check`: «como mucho 4 sockets por proceso»), que ya no depende de la basura de la pila.

## Prueba nueva: `tests/test_web_maquetar_352.c` (en `make ci`)

Tiene 38 comprobaciones con el intérprete y el DOM reales, contando los avisos de maquetar:

* el mismo valor y un valor nuevo en cada tipo de escritura;
* plantillas y contadores fuera del documento;
* meter y sacar un nodo del documento;
* el formato de `style`;
* que el DOM queda con los valores escritos;
* que un `MutationObserver` sigue recibiendo sus registros;
* el análisis de JavaScript: mensajes de error con la misma columna que antes, flechas y paréntesis, operadores y nombres que
  empiezan como una palabra reservada (`dox`, `instanceofx`).

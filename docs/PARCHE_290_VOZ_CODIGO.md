# Parche 290 · SOMA habla mientras programa, y las webs con JavaScript desde el orbe

## Qué pasaba

- Al pedir un programa desde el orbe o el chat, SOMA daba el aviso de salida y luego se quedaba callada varios minutos.
- Al terminar, la voz leía el informe técnico tal cual: «Programa listo: snake. Python: compila y pasa las comprobaciones. Ejecutalo
  con: python snake. O abrelo en SOMA Studio… 3 llamadas al modelo, 0 reparaciones…».
- «Hazme una web para mi tienda» no llegaba al programador, porque «web» no contaba como algo que programar. Si llegaba, se programaba
  en SOMA-LANG y no como página HTML.

## ¿Se puede meter JavaScript dentro de un HTML?

**Sí.** Ya funcionaba desde el parche 275:

- El programador crea páginas de un solo archivo `.html` con el CSS en `<style>` y el JavaScript en `<script>`.
- Antes de guardarla, la comprueba: etiquetas, ids, enlaces y la **sintaxis de cada `<script>`** con el motor de JavaScript de SOMA,
  con la línea de cada error.
- El navegador de SOMA ejecuta ese JavaScript con su propio motor: DOM, eventos, temporizadores, `localStorage`…
- Lo único que aún no hay es el **canvas 2D** (`getContext`). El programador lo sabe y dibuja con `div` y CSS, o con SVG en línea.
- A mano: `html nuevo mi_web juego` crea un proyecto, `html comprobar`, `html empaquetar` lo junta todo en un archivo y `html abrir`
  lo abre en el navegador.

Lo que cambia ahora es que **desde el orbe** ya llegan al programador frases como «hazme una web para mi tienda», «crea una landing
de mi app», «genera una página html con un formulario» o «hazme un juego en javascript». Se hacen en HTML con su JavaScript dentro.
Las frases de escribir textos no cambian de camino: «escribe un artículo para mi web» o «redacta una descripción para mi sitio web»
no van al programador.

## Qué cambia

### SOMA cuenta lo que hace mientras programa (`src/agent/prog_voz.c`)

Las frases salen en el orbe y en voz alta si la voz está activada (Ajustes > Voz):

- **Hay un fallo:** «La primera versión tiene un fallo en la línea 12; lo estoy corrigiendo.»
- **Lo vuelve a intentar:** «Sigo corrigiendo: ahora el fallo está en la línea 40.»
- **Cambia de enfoque:** «No terminaba de salir bien: pruebo con una versión más sencilla.»
- **Revisión final:** «Ya funciona. Le doy un último repaso para comprobar que hace todo lo que pediste.»

Para no resultar pesada, no corta lo que se esté diciendo, espera al menos 8 segundos entre frases y no repite la misma idea en
cada reparación.

### Al terminar, un resumen para escucharlo

El modelo explica en dos o tres frases qué hace el programa y cómo se usa. SOMA lo dice, por ejemplo: «Listo: he creado «snake»
en Python y funciona. El juego dibuja una serpiente que mueves con las flechas; cada manzana suma un punto. Lo tienes abierto en
SOMA Studio.»

- Si era un cambio: «He hecho los cambios en…».
- Si era una web: «He creado la página web…, con el HTML, el estilo y el JavaScript en un solo archivo».
- Si no salió: dice la línea del último fallo, que el archivo queda como estaba, y propone pedirlo con más detalle o más sencillo.

El texto de la respuesta empieza con ese mismo resumen y sigue con el informe técnico de siempre. La voz solo dice el resumen
(`agent_voz_toma`). Si el modelo no da la explicación, el resumen sale sin ella. Solo pasa con lo que pide el usuario; lo que manda
otro agente no.

### Otros detalles

El aviso de salida dice también «en HTML» o «en JavaScript» cuando toca. Antes decía SOMA-LANG.

## Pruebas

- `tests/test_prog_voz_290.c` (29 comprobaciones):
  - las frases de cada fase, cuándo se calla y que no repita;
  - los nombres y los lenguajes;
  - la limpieza de la explicación: sin razonamiento, Markdown ni código, tres frases como mucho y sin letras partidas;
  - el resumen final de cada caso.
- `tests/test_web_html_290.c` (16 comprobaciones): «una web» y «web para» como palabras, sin confundirlas con «webcam». ASan encontró
  aquí una lectura fuera del texto en la primera versión y está corregida.
- Las 2344 frases del catálogo del LCM llegan al programador exactamente igual que antes.
- Siguen pasando las pruebas del programador (235, 239, 265, 275 y coder_sim), las de rutas y las del LCM.

Hace falta `make`.

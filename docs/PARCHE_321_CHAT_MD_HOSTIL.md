# Parche 321 · El maquetador de Markdown del chat ya no lee fuera de su memoria

**Hace falta `make`.**

## El problema
`chat_md.c` maqueta en el núcleo lo que escriben los modelos en el chat: Markdown con tablas, listas, código, LaTeX y algo de HTML.
Lo que escribe un modelo lo puede dirigir una página con instrucciones escondidas que haya leído el agente, así que es una entrada
de fuera. Tenía una prueba con 3000 textos al azar; un fuzzer nuevo que **genera Markdown con estructura** (tablas que no cuadran,
listas y citas muy anidadas, código sin cerrar, LaTeX, entidades enormes, HTML, enlaces e imágenes rotos), con columnas de 1 a
900 px, capacidades pequeñas y **cada búfer de su tamaño exacto** (como `chat.c`, que reserva el pool con `kmalloc`) encontró:

1. **Lecturas fuera del pool cuando se llena.** `pool_put`, si no cabe, marca `full` y no copia nada, pero nueve llamadas guardaban
   igualmente la longitud pedida: la palabra, el enlace, la imagen o el bloque de código apuntaban a bytes **más allá de lo
   escrito**, y medirlos (durante la maquetación) y dibujarlos (en `chat.c`) leía fuera del pool. En el sistema el pool es de
   768 KiB: basta un mensaje enorme, por ejemplo un bloque de código de cientos de KB (el código se guarda dos veces, una para el
   botón Copiar).
2. **`add_img`** preguntaba el tamaño de la imagen con la longitud original aunque no hubiera cabido.
3. **`u8back`**, al partir una palabra más ancha que la línea, miraba el byte siguiente al final de la palabra (fuera del pool si
   era la última).

## Qué cambia (`src/apps/chat_md.c`)
* `pool_dup`: como `pool_put`, pero si no cabe la longitud queda a 0. La usan las nueve llamadas.
* `add_img` usa lo que cupo; `u8back` no lee el final de la palabra (siempre es un límite válido).
* **Red de seguridad** al final de `md_layout`: ninguna operación ni zona apunta fuera de lo escrito en el pool. Comprobado que
  sin ella las correcciones bastan solas; queda por si un cambio futuro olvida una.
* Los mensajes normales se maquetan igual.

## Pruebas
* **Nueva** `tests/test_chat_md_hostil_321.c` (en `make test` con 20 000 documentos; probado con 300 000): sin fallos de ASan ni de
  UBSan con gcc 14 y clang. Con el `chat_md.c` de antes, ASan lo para en los primeros miles.
* Siguen pasando `test_chat_285` (89/89) y `test_chatapp_285` (115/115).

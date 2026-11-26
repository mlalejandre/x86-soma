# Parche 322 · El motor LaTeX con datos hostiles

**Hace falta `make`.**

El parche 321 encontró en el maquetador de Markdown del chat que, con el pool lleno, quedaban textos apuntando más allá de lo
escrito. `tex.c` (el motor LaTeX) tiene la misma estructura y lo usan el **visor de documentos** (archivos `.tex` de cualquier web),
el maquetador y las fórmulas del chat (lo que escribe el modelo). Su prueba (`test_tex`) usa búferes estáticos grandes, que ocultan
justo este fallo.

## Lo que encontró el fuzzer nuevo (`tests/test_tex_hostil_322.c`)
Documentos LaTeX generados con estructura (secciones, listas, tablas, figuras, verbatim, fórmulas anidadas con `\left`, matrices,
`\frac`, `\sqrt`...), con el texto sin NUL detrás y las operaciones y el pool **de su tamaño exacto**, columnas de 1 a 960 px y
capacidades pequeñas:

1. **Textos fuera del pool.** `pool_put`, si no cabía, devolvía 0, y la operación se creaba igual con la longitud entera: `s = 0,
   n = 294` en un pool de 99 bytes. Quien dibuja (visor, maquetador, chat) lee `n` bytes desde ahí: fuera del pool. Lo mismo para
   los nodos de las fórmulas, los elementos y las etiquetas que se guardan para después.
2. **Desbordamiento de enteros** en el tamaño de un delimitador que crece: `(2 * half) * 100` con una fórmula enorme. En el núcleo
   (`-fwrapv`) daba la vuelta y la línea siguiente lo arreglaba de casualidad; en C es comportamiento indefinido.

## Qué cambia (`src/apps/tex.c`)
* `pool_put` devuelve el final de lo escrito si no cabe; `pool_dupn` deja la longitud a 0 (nodos, elementos, etiquetas); `op_add`
  anula el texto de una operación que se salga del pool.
* Red de seguridad al final de `tex_typeset` (comprobado que sin ella las correcciones bastan solas).
* El tamaño de un delimitador que crece queda acotado a 4000 px (va en un `int16_t`), sin desbordar.
* Los documentos normales se maquetan igual.

## Pruebas
* **Nueva** en `make test` (20 000 documentos; probado con 60 000): sin fallos de ASan ni de UBSan con gcc 14 y clang.
* Siguen pasando `test_tex`, `test_chat_285` (89/89), `test_chatapp_285` (115/115) y `test_visor_maquina_289` (43/43).

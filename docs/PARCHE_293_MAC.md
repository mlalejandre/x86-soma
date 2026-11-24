# Parche 293 · Las comprobaciones de los parches 286 a 292 en macOS

## Qué pasaba

Al aplicar los parches en el Mac (Darwin arm64), `--comprobar` dijo «HAY FALLOS» en el 286, el 287, el 289 y el 292. El código de
los parches estaba bien: todos los archivos salían «ok». Lo que fallaba eran las pruebas que lanzaba cada parche.

- **286 y 287 (`make pruebas-lcm`).** El parche 286 declaró `youtube_activo` como símbolo débil (`__attribute__((weak))`) en
  `lcm_soma.inc`, para que las pruebas del LCM funcionaran sin la app de YouTube.
  - En Linux, un símbolo débil que nadie define vale 0, y las pruebas pasaban.
  - El enlazador de macOS no lo admite: `test_lcm_257`, `test_aviso_266` y `test_lcm_286` no enlazaban.
  - `make pruebas-lcm` se paraba ahí, y con él la comprobación del 286 y la del 287, que usaba las mismas pruebas.
- **289 y 292.** Las pruebas rápidas pasaron en el Mac (48/48 y 53/53). Las pruebas «completas» (`test_visor_maquina_289` y
  `test_paint_ia_maquina_292`) compilan medio sistema con las cabeceras del núcleo.
  - `make test` ya las exige solo en Linux x86_64 y en otro equipo las omite diciéndolo, como `test_chatapp_285`.
  - La comprobación de los parches no aplicaba esa regla, y además no enseñaba el error, así que no se veía por qué fallaban.

## Qué cambia

- `src/agent/lcm_soma.inc`: el símbolo débil pasa a ser un puntero, `lcm_youtube_activo`, que vale 0 si no hay YouTube.
- `src/apps/youtube.c`: lo pone `youtube_init` al arrancar la app. El comportamiento es el mismo: «pausa el vídeo» es de YouTube
  solo si hay un vídeo abierto en YouTube. En las pruebas no hay YouTube, así que es de SOMA Media, como antes.
- Ninguna prueba del anfitrión deja ya símbolos débiles sin definir. Se ha comprobado con `nm` en todas.
- Los parches que genere a partir de ahora enseñan también los errores (stderr) cuando una prueba falla.

## La comprobación de este parche

- `make pruebas-lcm`, que en el Mac vuelve a enlazar.
- Las dos pruebas completas con la misma regla que `make test`. En Linux x86_64 tienen que pasar. En otro equipo, si no compilan, se
  omiten y se enseña el primer error del compilador; si compilan, tienen que pasar.

Hace falta `make` (toca el núcleo).

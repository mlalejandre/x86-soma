# Parche 303 · La prueba del lanzador en macOS

Solo cambian una prueba y una opción del lanzador. **No hace falta `make`.**

Con el 302, la tarea de macOS ya pasaba todas las pruebas de C. Se paraba en `tests/test_lanzador_emb_278.py`: la primera
petición al lanzador daba «timed out» al conectar.

* La prueba daba como mucho 10 s al lanzador para arrancar y seguía aunque no hubiera conectado nunca. En el Mac de GitHub
  (Python 3.15 de Homebrew) eso no basta, y además el lanzador escuchaba en `0.0.0.0`, todas las interfaces, que es lo que el
  cortafuegos de macOS vigila.
* `tools/soma_lanzador.py` lee `SOMA_LANZADOR_HOST`. Si no está, sigue escuchando en `0.0.0.0`, así que tu lanzador de siempre
  no cambia.
* `tests/test_lanzador_emb_278.py`:
  * el lanzador de la prueba escucha solo en `127.0.0.1`;
  * se espera hasta 40 s a que acepte conexiones de verdad;
  * si no llega, la prueba falla enseñando lo que dijo el lanzador, en vez de un «timed out» sin explicación.

Aquí la prueba da 20 comprobaciones y 0 fallos.

No tengo un Mac para confirmar la causa. Si vuelve a fallar, el mensaje dirá ya por qué.

# Parche 304 · Los servidores falsos de las pruebas con el mismo Python

Solo cambian pruebas. **No hace falta `make`.**

Con el 303, el lanzador ya contestaba en el Mac de GitHub. Lo que no arrancaba era el `llama-server` falso de la prueba: seguía
«cargando» a los 16 s y su puerto no aceptaba conexiones. Ese falso es un script con `#!/usr/bin/env python3`. En macOS, el
primer `python3` del PATH puede no ser el de la prueba (Homebrew 3.15), o ser el aviso de Xcode, y no llega a servir.

* `tests/test_lanzador_emb_278.py`, `tests/test_yt_service_250.py` y `tests/test_yt_service_251.py`: el falso se escribe con
  `#!` seguido de la ruta del mismo Python que corre la prueba (`sys.executable`).
* En la prueba del lanzador, la espera hasta «listo» pasa de 15 a 40 s.

Aquí: `test_lanzador_emb_278` da 20 comprobaciones y 0 fallos; `yt_service_250` y `yt_service_251` dan OK.

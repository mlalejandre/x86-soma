# PARCHE 271 · El LCM aislado y reentrenable

Requiere 268, 269 y 270 (y 265, el compilador `cc`). Responde al punto 5 de la hoja de ruta: «aislar al LCM para que pueda ser entrenado
si aumentan las habilidades del kernel, comandos, aplicaciones…».

## Qué cambia
* **Manifiestos** (`lcm/manifiestos/*.json`): cada comando/aplicación nueva se declara ahí y el LCM la aprende al reentrenar, sin tocar el
  catálogo ni el traductor. El validador comprueba que el comando existe en el núcleo; el informe de cobertura lista lo que el LCM no sabe
  (al empezar: 203 comandos del núcleo, 78 conocidos; ahora `cc` incluido y 63 internos excluidos a propósito en `_sin_lcm.json`).
* **Reentrenamiento con puerta** (`tools/lcm_ordenes/reentrena.py` + `gate.py`): entrena 2 redes, mide con los bancos fijos de siempre
  (exámenes 1-5, estrés, variantes, 2281 frases UD) y solo se aplica si no hay regresión. Las pruebas de cada manifiesto (frases nuevas,
  nunca entrenadas) miden si aprendió lo nuevo.
* **Modelo versionado en DATOS**: el núcleo lee `/lcm/modelo.lcm` (paquete LCMP: versión, fecha, manifiestos, métricas, suma de comprobación).
  Si falta, está dañado, truncado o no es válido, usa el de fábrica y lo dice (`lcm modelo`). `lcm modelo fabrica|datos` elige sin tocar nada.
* **Aislamiento**: `/lcm` es de solo lectura para agentes (`agent_path_protected`) y para el usuario (política del 268). Se instala desde
  fuera con `tools/datos.py DISCO escribir /lcm/modelo.lcm ARCHIVO`.
* **`cc`** es la primera capacidad declarada por manifiesto: «compila hola.c», «compila y ejecuta hola.c» (esta última pide confirmación).

## Pruebas
`python3 tests/test_lcm_271.py` (manifiestos, paquete, puerta, y que el C acepta un paquete hecho por Python) · `tests/test_lcm_257.c`
(cargador: paquete bueno, byte cambiado, truncado, sin magia, formato desconocido, borrado, fábrica/datos) · `tests/test_agent_policy.c` (`/lcm`).
No hace falta `make test` completo.

# LCM: cómo se enseña lo nuevo (parche 271)

El LCM (el traductor de peticiones a órdenes) vive aislado del núcleo: se **entrena fuera de SOMA**, se **mide con bancos fijos** y solo se
instala si **no empeora nada**. Dentro de SOMA nadie puede cambiarlo (ni el usuario ni los agentes: `/lcm` está protegido).

## Cuando SOMA gana un comando, una aplicación o una capacidad

1. Escribe `lcm/manifiestos/NOMBRE.json` (ejemplo: `cc.json`):
   - `ordenes[]`: `id`, `orden` (con piezas `{A}` archivo, `{D}` carpeta, `{N}` número, `{T}` texto...), `peligrosa` (true = pedirá confirmación),
     `frases` (al menos 6; patrones `(a|b)` y `[opcional]`), y opcionales `infinitivos`, `preguntas`, `valores` (ejemplos extra de una pieza).
   - `pruebas`: frases **nuevas** (no vistas al entrenar) con la orden exacta esperada. Son el examen de lo nuevo.
2. `python3 -m tools.lcm_ordenes.manifiestos validar`: comprueba formato, que el comando exista en el núcleo y que las piezas cuadren.
3. `python3 -m tools.lcm_ordenes.manifiestos cobertura`: comandos del núcleo que el LCM aún no sabe pedir (los internos se listan en `_sin_lcm.json`).
4. `export LCM_UD=carpeta_con_corpus_UD` y `python3 -m tools.lcm_ordenes.reentrena` (unos 40 min en 2 CPUs): entrena 2 redes, las mide con todos
   los bancos y las compara con `lcm/modelos/base_metricas.json`. **Si empeora algo, no se aplica** (el informe dice qué).
5. Si es apto queda `lcm/modelos/lcm-FECHA-HASH.lcm` (+ informe `.txt`). Se instala en el pendrive desde fuera de SOMA:
   `sudo python3 tools/datos.py DISCO escribir /lcm/modelo.lcm lcm/modelos/lcm-FECHA-HASH.lcm`
   o, para que sea el de fábrica, `reentrena --instalar-incorporado` y recompilar.

Dentro de SOMA: `lcm modelo` dice cuál se usa (DATOS o fábrica), su versión y, si el de DATOS no sirve (dañado, truncado...), por qué.
`lcm modelo fabrica` / `lcm modelo datos` eligen sin tocar `/lcm`.

## Reglas de la puerta (`tools/lcm_ordenes/gate.py`)
Cero peligrosas sin confirmar · el total no pierde órdenes exactas ni gana errores · ningún banco baja más de 2 puntos ni sube más de 0,5 de
errores · ninguna frase normal (UD) pasa a ejecutar algo · lo nuevo ≥ 75 % exacto y sin errores sin preguntar.

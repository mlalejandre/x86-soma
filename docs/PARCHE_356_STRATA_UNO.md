# Parche 356 · Solo tu perfil de Strata en la lista de modelos

**No hace falta `make`**: solo cambian el lanzador (`tools/soma_lanzador.py`, que hay que copiar otra vez al servidor) y
`tools/llm_server.py` (el menú de `./ejecutar.py`). Requiere el 355.

## El fallo

El 355 ofrecía **todos** los perfiles de `~/strata-custom`, también los de pruebas. En tu servidor el menú de `./ejecutar.py`
salía con 19 perfiles de Strata. Además, el menú del Mac mostraba `bge-m3` (el modelo de embeddings de la memoria) como si fuera un
LLM; el lanzador ya lo ocultaba desde el parche 278.

## Ahora

* Solo sale **el perfil que lanzas con tu script**: `iq3s-latin-vision-120k.json`, como `strata-iq3s-latin-vision-120k`.
* Con la variable **`STRATA_PERFIL`** se elige otro, varios separados por comas o todos (`*`). La lee el lanzador del servidor (y
  `--instalar-servicio` la conserva) y también `./ejecutar.py` en el Mac.
* `bge-m3` y lo que esté en una carpeta `embeddings/` ya no salen en el menú de `./ejecutar.py`.

## Pasos

1. Copia `tools/soma_lanzador.py` al servidor y reinicia el lanzador (`systemctl --user restart soma-lanzador`).
2. Nada más: `./ejecutar.py` ya usa el `tools/llm_server.py` nuevo.

## Prueba

`tests/test_strata_355.py` (en `make ci`) tiene 4 comprobaciones nuevas, 28 en total:

* un perfil de pruebas (`q4-helper-latin.json`) no sale en el lanzador ni en el menú del Mac;
* solo sale el elegido;
* `bge-m3` no sale como LLM;
* con `STRATA_PERFIL=*` salen todos.

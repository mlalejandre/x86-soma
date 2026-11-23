# PARCHE 278 · Arrancar y parar el servidor de embeddings desde SOMA

La memoria (277) necesita un servidor `llama-server --embeddings` con bge-m3. Igual que el del LLM, ahora se arranca **desde SOMA**,
sin SSH y sin que arranque solo con el servidor, usando el lanzador que ya existe (`tools/soma_lanzador.py`, puerto 8086, misma clave).

## En SOMA
    memoria servidor            estado (listo / cargando / parado)
    memoria servidor arrancar   lo arranca y espera a que este listo
    memoria servidor parar      lo para (la memoria sigue funcionando por palabras)
    memoria probar              comprueba que SOMA lo alcanza (puerto 8089 en la misma IP que el LLM)

## En el servidor (una sola vez)
1. Copiar `tools/soma_lanzador.py` nuevo al servidor, en el mismo sitio que el anterior.
2. Reiniciar el lanzador (si es servicio de usuario: `systemctl --user restart soma-lanzador`; si lo lanzas a mano, Ctrl+C y de nuevo).
   La clave no cambia. Al arrancar escribe una linea `embeddings (memoria de SOMA): bge-m3-Q8_0.gguf en el puerto 8089`.

## Qué hace el lanzador
* `GET /v1/embeddings/estado`, `POST /v1/embeddings/lanzar`, `POST /v1/embeddings/parar` (misma clave y mismas restricciones de red que el resto).
* Busca el primer `bge-m3*.gguf` (prefiere el Q8) bajo `MODELS_DIR` y lanza `llama-server --embeddings --pooling cls -ngl 0 -c 8192`
  en **CPU** (`CUDA_VISIBLE_DEVICES` vacio): no quita VRAM a las GPU del LLM. Binario: `/opt/llama.cpp/build/bin/llama-server` si existe.
* Es independiente del servidor del LLM: su propio proceso, PID (`/tmp/soma-emb.pid`) y registro (`/tmp/soma-emb.log`); parar uno no toca al otro.
  Se niega si el puerto de embeddings fuese el del LLM.
* Variables opcionales: `SOMA_EMB_PORT` (8089), `SOMA_EMB_BIN`, `SOMA_EMB_MODEL`, `SOMA_EMB_THREADS` (4), `SOMA_EMB_CTX`, `SOMA_EMB_EXTRA_ARGS`.
* Cambio de comportamiento: `/v1/modelos` ya no lista bge-m3 ni lo que este en una carpeta `embeddings/` (no son LLM y no se pueden lanzar como tales).

## Pruebas
`make pruebas-memoria`: el protocolo en C (12 comprobaciones) y el lanzador real contra un `llama-server` falso (19: clave, arranque, parametros,
el LLM sigue vivo antes/durante/despues, parar, relanzar, sin modelo, mismo puerto). Lo que NO se ha probado aqui: el `llama-server` real de tu servidor
y la orden en SOMA (solo compilada).

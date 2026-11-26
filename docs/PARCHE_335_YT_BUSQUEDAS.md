# Parche 335 — YouTube: el buscador con IA propone búsquedas de verdad

## El fallo
Con el buscador con IA activado, la tarjeta decía «Sin IA (el modelo no ha propuesto búsquedas): se ha buscado el texto tal
cual» y se buscaba la frase entera («quiero ver vídeos de cómo hacer pan casero sin amasar, por favor»).

Causa: la petición al modelo tenía un límite de **200 fichas**. Un modelo que razona (Qwen3, DeepSeek-R1, los de LM Studio con
razonamiento...) las gasta pensando y no llega a contestar; llm.c devuelve entonces solo el razonamiento (o lo pone delante de
la respuesta) y sus líneas, frases largas, no valen como búsqueda. Resultado: cero búsquedas y la frase tal cual.

## Qué cambia
- Hasta **1200 fichas** para proponer las búsquedas, con sitio para el razonamiento (búfer de 8 KB).
- De la respuesta se queda **solo la respuesta**: fuera `[Razonamiento]: …`, `<think>…</think>` y, si el razonamiento se quedó
  sin cerrar, no se toma nada de él.
- Una línea de más de 9 palabras ya no cuenta como búsqueda (es una frase de explicación).
- Si aun así no hay búsquedas (o no hay modelo), la búsqueda **se saca de la petición**: sin «quiero ver vídeos de»,
  «ponme», «¿puedes buscarme…?», «en YouTube», «por favor», «gracias»… («quiero ver vídeos de cómo hacer pan casero sin
  amasar, por favor» → «cómo hacer pan casero sin amasar»). La tarjeta enseña lo que se buscó de verdad.
- Con el buscador con IA desactivado se sigue buscando exactamente lo escrito.

## Pruebas
- `test_yt_busquedas_335` (en `make test`): respuestas con razonamiento en los dos formatos, razonamiento sin cerrar, frases
  largas, búsquedas de siempre, peticiones en lenguaje natural con tildes y signos de apertura, y 20000 entradas al azar.
- `test_youtube_ia_251` sigue igual (54434 comprobaciones).
- `python3 tests/prueba_yt_335.py` (QEMU, sin modelo): «quiero ver videos de gatos graciosos en youtube, por favor» busca «gatos graciosos».
- Necesita `make`.

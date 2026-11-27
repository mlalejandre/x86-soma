# Parche 355 · Strata en la lista de modelos del servidor, y consultas con imágenes

**Hace falta `make`.** Requiere el 354. **En el servidor hay que copiar `tools/soma_lanzador.py`** y reiniciar el lanzador (paso 1).

## Qué cambia

### Strata como un modelo más

El servidor (192.168.1.200) tiene ahora, además de los `.gguf` de llama-server, el motor **Strata**, que se arranca con un perfil
JSON (`~/strata-custom/iq3s-latin-vision-120k.json`, Qwen 3.8 Flash Next IQ3_S, 120 000 tokens de contexto, visión).

* **Cada perfil de `~/strata-custom/*.json` sale en la lista de modelos** como `strata-<perfil>`; el tuyo es
  `strata-iq3s-latin-vision-120k`. Esto vale:
  * en el portátil x86: Ajustes > Inteligencia artificial > Lanzador, o `llm-config lanzador modelos`;
  * en QEMU: el menú de `./ejecutar.py`, que lo lanza por SSH.
* **Se lanza como lo hace tu script**, con una diferencia: SOMA tiene que llegar a él por la red local.

      strata serve --profile ~/strata-custom/<perfil>.json --host 0.0.0.0 --port 8087 --parallel 1

  * Escucha en **la red local y en el puerto del LLM de siempre (8087)**, igual que llama-server. Por eso SOMA lo usa sin
    cambiar nada: la API es la compatible con OpenAI (`/v1/chat/completions`, `/v1/models`).
  * Tu script del Mac lo arranca en `127.0.0.1:18081` con un túnel SSH: así SOMA, en el portátil, no podría llegar a él.
  * **Sin `CUDA_VISIBLE_DEVICES`**: el perfil reparte las GPU (3090 y 4090). llama-server sigue igual, solo en la 4090.
  * Strata se busca en el `PATH`, `~/.local/bin` y `~/bin`. Si lo tienes en otro sitio, usa `STRATA_BIN`; si los perfiles
    están en otra carpeta, `STRATA_PERFILES`.
* **Un perfil roto** (JSON que no se lee) no se ofrece.
* **Cambiar de modelo** para Strata con todo su grupo de procesos, incluido `strata-vision`, que no escucha en el puerto: antes
  solo se paraba lo que escuchaba en el puerto. Y al revés: lanzar Strata para el llama-server que hubiera.
* **No se lanzan dos Strata a la vez.** Si ya hay uno que no lanzó el lanzador (el de tu script del Mac, puerto 18081), SOMA dice
  «ya hay un Strata en marcha en el servidor… detenlo primero» y no toca el tuyo. Las GPU no darían para dos. Son dos formas de
  usar el mismo servidor: tu script (chat web en el Mac) o SOMA, una cada vez.
* **Un servidor sin `/health`** (Strata podría no tenerlo) se da por listo cuando `/v1/models` contesta. Esto vale en el lanzador,
  en `ejecutar.py` y en SOMA (`health`, Ajustes).
* **El nombre del modelo.** llama-server acepta cualquier nombre en las peticiones; Strata tiene el suyo y podría rechazar otro.
  Ahora SOMA lo adopta en dos casos:
  * al lanzar un modelo desde SOMA, toma el nombre que da el servidor;
  * al comprobar el servidor, si este sirve **un solo** modelo y no es el configurado (la ruta de `ejecutar.py`, que no le dice a
    SOMA el nombre). Con llama-server no cambia nada: ya sirve el alias.
* **Modelos `.gguf` con visión:** si en la carpeta del modelo hay un `mmproj*.gguf`, llama-server se lanza con `--mmproj`. Antes
  ningún modelo de llama-server veía imágenes.

### Consultas con imágenes

* El **chat en modo Solo LLM** ya mandaba las imágenes adjuntas desde el parche 248, igual que el agente de documentos con sus
  capturas. Con un modelo con visión, como este perfil de Strata, ahora contestan sobre ellas.
* **Orden nueva `mirar <ruta|pantalla> [pregunta]`.** Manda la imagen al modelo en uso y escribe lo que contesta.
  * Lee PNG, JPEG, GIF y WebP de hasta 6 MiB, del sistema de archivos o del almacén (`/big/...`, las capturas de `/capturas`).
  * `mirar pantalla` captura la pantalla en ese momento.
  * La usa el **agente**: está en su catálogo de órdenes, como `cat`. En el chat en **modo Agente**, al adjuntar archivos la
    misión le dice que las imágenes se ven con `mirar`. Antes el agente solo recibía la ruta y no podía verlas.
  * Si el modelo no tiene visión, lo dice y explica cómo elegir uno que la tenga.

## Pasos

1. **En el servidor**, una vez: copia el `tools/soma_lanzador.py` nuevo encima del anterior y reinicia el lanzador.
   * Como servicio: `systemctl --user restart soma-lanzador`.
   * A mano: Control+C y vuelve a lanzarlo.

   La clave no cambia.
2. **Si tu script de Strata está en marcha, páralo** (Control+C): las GPU no dan para dos.
3. **Portátil x86:** Ajustes > Inteligencia artificial > Lanzador > modelos y elige `strata-iq3s-latin-vision-120k`. Desde el
   terminal es `llm-config lanzador modelos` y luego `llm-config lanzar strata-iq3s-latin-vision-120k`. Con visión tarda más en
   cargar: para los perfiles de Strata la espera es de hasta 10 minutos (4 para los `.gguf`).
4. **QEMU:** `./ejecutar.py` muestra el menú con los perfiles de Strata al final, marcados `[Strata, vision]`. Si ya hay un modelo
   en marcha, contesta `y` a «¿Cambiar de modelo?», o usa `./ejecutar.py --modelo`. La espera de Strata es de hasta 10 minutos
   (`STRATA_HEALTH_TIMEOUT_S`). Al salir de QEMU se para, igual que llama-server, salvo con `--dejar-servidor`.
5. **Pruébalo con imágenes:**
   * en el chat (Solo LLM), adjunta una imagen y pregunta;
   * en modo Agente, adjúntala y pide lo que quieras;
   * en el terminal, `mirar pantalla ¿qué ves?` o `mirar /usuario/Imágenes/foto.jpg describe la foto`.

## Lo que he supuesto de Strata (no he podido ver tu instalación)

* Su API es la compatible con OpenAI: `/v1/models`, y `/v1/chat/completions` con imágenes como `image_url` en `data:...;base64`.
  Es lo que muestra tu script («API: http://localhost:28081/v1»).
* Acepta `--host 0.0.0.0` y `--port` como en tu script, y `--parallel 1`.
* Si tiene `/health`, se usa; si no, `/v1/models`.
* Si el perfil reparte las GPU por su cuenta, no hace falta `CUDA_VISIBLE_DEVICES`.

Si algo de esto no se cumple, el lanzador deja el registro en `/tmp/soma-llama.log` (el de siempre) y SOMA muestra sus últimas
líneas.

## Pruebas

* **`tests/test_strata_355.py`** (en `make ci`, dentro de `pruebas-memoria`). Usa el lanzador real como proceso y un Strata falso
  que hace lo peor que podría hacer el de verdad: no tiene `/health`, rechaza cualquier nombre de modelo que no sea el suyo, arranca
  un hijo `strata-vision` y tarda en cargar. Tiene 24 comprobaciones:
  * la lista (perfiles con su visión, `.gguf` con `mmproj`, el `mmproj` y un perfil roto fuera);
  * los argumentos de Strata;
  * que no lleva `CUDA_VISIBLE_DEVICES`;
  * listo sin `/health` y con el nombre de Strata;
  * cambiar a llama-server (para el hijo y lanza con `--mmproj`);
  * negarse con otro Strata en marcha sin tocarlo;
  * en `llm_server.py`, la lista, lanzar, esperar y parar con su hijo, con «SSH» hecho con bash local.
* **`tests/prueba_strata_qemu_355.py`** (QEMU, a mano, unos 4 minutos). SOMA de verdad contra el lanzador real y el Strata falso.
  Tiene 8 comprobaciones:
  * SOMA ve el perfil, lo arranca y queda con el nombre de Strata;
  * `mirar pantalla` llega como un PNG de 206 KB en `image_url`;
  * un archivo que no existe da error;
  * `health` sin `/health` vuelve al nombre de Strata después de cambiarlo a mano (la ruta de `ejecutar.py`).
* Compila sin avisos con gcc-14 en x86_64 y aarch64.

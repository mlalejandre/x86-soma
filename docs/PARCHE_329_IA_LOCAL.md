# Parche 329 · Motor de IA local (tareas 0-A y 0-B)

**Hace falta `make`.** La primera compilación tarda más: son unos 240 archivos nuevos de llama.cpp, que se compilan en paralelo.

## Qué pedías
* **0-A**: que la memoria pueda calcular sus *embeddings* en SOMA mismo, sin depender del servidor (que no arrancaba o no contestaba a
  `/v1/embeddings`), y conservando el servidor como opción.
* **0-B**: ejecutar en SOMA LLM pequeños como LFM2.5-350M, de la forma más eficiente posible. Y la duda sobre la coma flotante.

## La coma flotante
El **núcleo** no usa coma flotante a propósito (`-mgeneral-regs-only`), igual que Linux: así no tiene que guardar los registros SIMD
en cada interrupción. Los **programas de ring 3** sí tienen SSE, AVX2, FMA y F16C, con su estado guardado en cada cambio de
proceso. Ya lo usaba el reconocimiento de voz (whisper). Por eso el motor va en ring 3: es más seguro (un fallo en el modelo no
tumba el sistema) y corre a plena velocidad. No había que cambiar nada del núcleo para tener coma flotante.

Sí hacía falta algo pequeño en el núcleo: la **base de FS** de cada proceso (`SYS_SOMA_FS`, como `arch_prctl` en Linux). La
biblioteca de C++ del sistema viene compilada con protector de pila, que lee su canario en `%fs:0x28`. Sin base de FS, el programa
moría al arrancar. Se guarda por proceso, se pone al cambiar de proceso y también en los hilos de cálculo de los otros núcleos.

## Qué cambia
* **`ia.elf`** (nuevo, `src/user/ia/`): llama.cpp y ggml (`third_party/llama`, MIT, noviembre de 2026) en un proceso de ring 3:
  * con **AVX2/FMA** y **varios hilos** de cálculo en los núcleos libres, como la voz;
  * **un solo proceso que vive mientras se usa**: carga cada modelo una vez y se para solo tras 10 minutos sin uso (o con
    `ia parar`), y entonces devuelve toda la memoria;
  * el **monton** es una región grande bajo demanda (solo gasta lo que toca); va comprimido en la imagen (2,4 MB);
  * llama.cpp avisa de los errores con **excepciones de C++**: ahora funcionan en ring 3 (libsupc++ y el desenrollador de libgcc
    enlazados, tabla `.eh_frame` registrada al arrancar). Un modelo roto da un error claro y el motor sigue vivo.
* **`src/arch/x86_64/ia_sys.c`** (nuevo): lanza y vigila el proceso y le pasa las órdenes por una región compartida
  (`src/core/ia_abi.h`). **El núcleo solo maneja texto**: la respuesta es el mismo JSON que da un servidor compatible con OpenAI,
  y el resto del sistema la trata igual.
* **Memoria (0-A)**: `memoria config emb_motor local|servidor|auto`. En `auto` (el valor por defecto), si está el modelo de
  `emb_modelo` (bge-m3) en `/modelos`, los vectores se calculan en SOMA. Si no, se usa el servidor de siempre. Si falta el modelo,
  se copia una vez de la carpeta MODELOS del pendrive. Los vectores son los mismos que daba el servidor con el mismo modelo.
* **LLM (0-B)**: nuevo proveedor **«En SOMA»** en Ajustes > Inteligencia artificial (y `llm-config proveedor soma`). El modelo es
  el nombre del `.gguf` (por ejemplo `lfm2` encuentra `LFM2.5-350M-Q4_K_M.gguf`). *Ver modelos* lista los instalados. Funciona con
  el chat, el agente y el orbe, con la plantilla de chat del propio modelo. El chat se puede cancelar.
* Orden **`ia`**: `ia estado | modelos | instalar <nombre> | chat <modelo> <texto> | embed <modelo> <texto> | tokens … | parar`.
* `fw_cfg`: quien lee un archivo grande por partes lo reserva. Antes, al instalar un modelo en QEMU, el orbe podía seleccionar otro
  archivo a la vez y el modelo quedaba corrupto (lo detectó la prueba).
* `tools/lote_cc.py` compila los 240 archivos de llama.cpp en paralelo y solo los que cambian.

## Cómo usarlo en el T480s
1. Copia los `.gguf` en la carpeta **MODELOS** del pendrive:
   * embeddings: `bge-m3-Q8_0.gguf`, el mismo que usaba el lanzador (`tools/soma_lanzador.py`);
   * LLM: por ejemplo `LFM2.5-350M-Q4_K_M.gguf` (unos 230 MB) o `Q8_0`.
2. En SOMA: `ia instalar bge-m3` e `ia instalar lfm2`. La memoria y el LLM también los copian solos la primera vez que los
   necesitan.
3. Ajustes > Inteligencia artificial > **En SOMA**, modelo `lfm2`, *Guardar y probar*.

Rendimiento esperado en el T480s (i5/i7 de 8.ª generación, 4 núcleos con AVX2): un modelo de 350M en Q4/Q8 genera del orden de
decenas de tokens por segundo. Un embedding de bge-m3 tarda unas décimas de segundo. No lo he podido medir en tu equipo: en QEMU
todo va mucho más lento, porque emula AVX2.

## Pruebas
* **`tests/prueba_ia_329.py`** (QEMU, 4 procesadores, 14 comprobaciones), con modelos diminutos generados por
  `tools/ia_modelos_prueba.py` (sin descargas). Comprueba:
  * la huella exacta de los modelos leídos desde ring 3;
  * los tokenizadores WordPiece y SentencePiece;
  * embeddings y generación voraz **idénticos** a los de llama.cpp en el anfitrión;
  * un modelo roto (excepción recogida, el motor sigue vivo);
  * la memoria con el motor local, el proveedor «En SOMA» e `ia parar`.
* **Anfitrión** (`make ia-host`): el mismo programa sobre Linux. Con los tokenizadores de prueba de llama.cpp
  (`models/ggml-vocab-*.gguf`) acierta 684 de 708 casos. Los 24 fallos son todos de un tokenizador (deepseek-llm, que usa
  expresiones regulares Unicode completas), que no usan ni LFM2 ni bge-m3. Un modelo BERT real (vocabulario de bge) y un LFM2 de
  prueba dan los mismos números que `llama-embedding` y `llama-simple` del llama.cpp oficial.
* `make sbx-check`, `fault-check`, `make ci` y compilación sin avisos en x86_64 y aarch64. En aarch64 no hay motor (falta EL0): la
  orden y el proveedor lo dicen.

## Límites
* Solo modelos de texto (el proveedor «En SOMA» rechaza imágenes con un mensaje claro).
* Un solo trabajo a la vez: mientras el LLM genera, la memoria espera para calcular un vector.
* La imagen del núcleo pasa del 58 % al 62,6 % de su presupuesto.

## Arreglo: enlazar en Debian (la imagen del `Dockerfile`)
La primera versión de este parche no enlazaba `ia.elf` con gcc 12 de Debian bookworm: su libstdc++ no está hecha con
`_FORTIFY_SOURCE`, así que pide `mbsrtowcs` y no `__mbsrtowcs_chk`; y su glibc (2.36) redirige `sscanf` a `__isoc99_sscanf`, no a
`__isoc23_sscanf`. `ia_libc2.c` trae ahora las dos variantes. El parche 339 añade a la CI una compilación en ese contenedor.

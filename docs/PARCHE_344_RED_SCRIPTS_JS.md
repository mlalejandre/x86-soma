# Parche 344 · Red del núcleo más ágil, scripts en paralelo y cadenas de JavaScript más baratas

**Hace falta `make`.** Requiere el 343. Son las causas 6, 7 y 8 de la lista de lentitud del navegador.

## 6 · Red del núcleo  <!-- SOMA_344_RED -->

* **`netd` despierta antes cuando hay tráfico** (`src/core/thread.c`). Antes dormía siempre 10 ms entre sondeos: en una descarga
  larga, cada ráfaga de la ventana esperaba hasta 10 ms a ser recogida. Ahora duerme 1 ms si ha recibido algo en los últimos
  200 ms y 10 ms en reposo, así que el consumo en reposo no cambia.
* **Más búferes de recepción:**
  * virtio-net: 64 en vez de 8 (`src/drivers/virtio_net.c`; nunca más de los que admite la cola del dispositivo);
  * Intel Wi-Fi: 128 en vez de 64 (`src/arch/x86_64/iwl.c`).
  Con 8, una ráfaga de más de 8 paquetes entre dos sondeos se perdía y forzaba una retransmisión.
* **Ventana de recepción TCP de 64 KiB** (antes 32 KiB, `src/net/tcp.c`). Con 50 ms de ida y vuelta, 32 KiB limitaban la
  descarga a unos 650 KB/s.
* **El RTO sigue en 1 s, a propósito.** La pila no mide el tiempo de ida y vuelta: bajarlo sin esa medida provocaría
  retransmisiones de más en enlaces lentos (Wi-Fi, móvil). Con los búferes nuevos las pérdidas locales casi desaparecen, que
  era lo que hacía saltar el RTO.

## 7 · Scripts descargados en paralelo  <!-- SOMA_344_SCRIPTS -->

* **Precarga** (`src/apps/web_page.c`). Mientras se ejecuta un script, los siguientes (hasta 3, clásicos o módulos) ya se
  están descargando con las corrutinas del 343. Siempre queda un hueco libre para las imágenes.
* **El orden de ejecución no cambia.** Solo se adelanta la descarga; cada script se ejecuta cuando le toca. Si el siguiente aún
  no ha llegado, el navegador lo espera (sin bloquear las demás descargas). Si no se pudo precargar, se descarga en síncrono,
  como antes. Una descarga que sobra (la página navegó, el nodo cambió) se libera.
* **Lista de bloqueo ampliada** con dominios de publicidad y seguimiento que solo cargan más scripts (googletagservices,
  smartadserver, adform, teads, seedtag, prebid, connect.facebook.net…). Los gestores de consentimiento **no** se bloquean:
  sin ellos algunas páginas no muestran el contenido.

## 8 · Cadenas de JavaScript  <!-- SOMA_344_JS -->

Corrección de la lista anterior: decía que el intérprete no tenía recolector de basura, y lo tiene desde el parche 189. Lo que
de verdad costaba eran las cadenas:

* **El hash se calcula solo cuando hace falta** (`js_str_hash`). Antes, cada cadena nueva (cada `+=`, cada concatenación)
  recorría todos sus bytes para calcular un hash que casi nunca se usaba. Ahora solo se calcula al buscar una propiedad,
  internar o usarla como clave de un `Map`. `js_str_eq` admite cadenas aún sin hash.
* **Las cadenas no se ponen a cero** (`js_gc_alloc` con `atomic == 2`). No contienen punteros y se escriben enteras justo
  después, así que el `memset` sobraba.

## Medido en QEMU

| | 343 | 344 |
|---|---|---|
| Archivo de 4 MB, 20 ms de retardo en el enlace | 4,1 s | **2,8 s** |
| Archivo de 4 MB, sin retardo | 0,40 s | 0,35 s |
| Página con 8 scripts, 50 ms de ida y vuelta | 0,8 s | **0,5 s** |
| JS: lista de 6000 `<li>` construida con `+=` | 3,5 s | **1,3 s** |
| Portada con 120 fotos | 2,6 s | 2,6 s (sin cambio) |

En el anfitrión, 20 000 `+=` pasan de 8,8 s a 2,3 s.

## Pruebas

* `tests/jsrun_host_274/run.sh` (en `make test`), dos casos nuevos:
  * «cadenas 344»: concatenación, comparación de cadenas sin hash, claves de objeto y de `Map`, `JSON.stringify`;
  * «+= de 20000»: la longitud final es exacta.
* `tests/test_tcp_rx.c` (en `make test`), adaptada a 64 KiB: el dato fuera de ventana ahora va más allá de 64 KiB, y el caso
  que llena el búfer manda 70 000 bytes, cierra la ventana en 65 535 y da la vuelta al anillo (el índice ya no es potencia
  de dos).
* La página con 8 scripts mantiene el orden («orden 0-1-2-3-4-5-6-7»).
* `tests/prueba_navegador_341.py` (QEMU): las 120 fotos con 4 GiB y con 512 MB.

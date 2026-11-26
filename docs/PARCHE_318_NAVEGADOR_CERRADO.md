# Parche 318 · Si el navegador aislado no arranca, la página no se abre en el núcleo

**Hace falta `make`.** Solo x86_64 (en aarch64 no hay ring 3 y el motor web va en el núcleo, como siempre).

## El problema
Desde el parche 202 el motor web entero (HTML, CSS y **JavaScript**) corre en un proceso aislado de ring 3. Pero `browser.c` tenía un
respaldo que **fallaba abierto**: si ese proceso no llegaba a arrancar ni una vez en la sesión (sin memoria, sin ranuras de proceso
y, desde el parche 314, si el programa incrustado no se descomprime), la sesión seguía **en silencio** con el motor **dentro del
núcleo**, con solo un aviso en la consola. Bastaba con agotar los recursos para que una página, con su JavaScript, corriera en
ring 0.

## Qué cambia
* `bx_navigate` (`src/apps/browser.c`): si el proceso aislado no arranca, la página **no se abre**. La barra de estado lo dice en
  ámbar («No arranca el navegador aislado (falta memoria o procesos): cierra algo y vuelve a intentarlo») y la consola también.
  La siguiente navegación lo vuelve a intentar, así que en cuanto haya recursos el navegador funciona sin hacer nada más.
* El motor del núcleo **solo** se usa si se pide expresamente en `/etc/navegador.conf` (`aislado=no`), como antes.
* Ya no hay ningún camino que pase de una sesión aislada al motor del núcleo.

## Pruebas
* **QEMU** (`make sbx-check`, bloque `SBX`): con el arranque del proceso aislado forzado a fallar (`browser_r3_prueba_fallo`,
  solo para la autoprueba), navegar devuelve error, la sesión sigue en modo aislado y la barra de estado lo explica. El resto de
  `sbx-check` (navegador real, red, TLS, huella) sigue bien.

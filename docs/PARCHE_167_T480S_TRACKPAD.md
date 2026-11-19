# Parche 167 · T480s: trackpad con gestos

Requiere el 166. Ficha 167 del plan. Hasta ahora el trackpad Synaptics funcionaba en **modo relativo**, el de fábrica: se
comporta como un ratón PS/2 y no dice cuántos dedos hay. Con este parche SOMA lo pone en **modo absoluto** (paquetes de
6 bytes con posición, presión y anchura del contacto), como hace Linux, y reconoce los gestos él mismo.

## Qué cambia
- **Gestos**:
  - un dedo mueve el puntero, con aceleración suave;
  - **toque = clic** (se puede desactivar);
  - **toque con dos dedos = clic derecho**;
  - **dos dedos arriba/abajo = desplazar** (en webs, el terminal y las listas);
  - **clic con dos dedos = clic derecho** (el T480s es un *clickpad*: toda la superficie es el botón);
  - **rechazo de palma**: los contactos muy anchos se ignoran;
  - al levantar o poner un dedo el puntero no salta (se descarta el primer paquete de cada contacto).
- **TrackPoint**: en modo absoluto sus paquetes llegan por el **canal de paso** (pass-through) del Synaptics. SOMA los separa
  y los entrega como siempre. TrackPoint + botón central sigue desplazando.
- **Configuración** en `/etc/trackpad.conf` (`modo`, `natural`, `toque`, `velocidad`), con la orden `trackpad`:
  - `trackpad`: modo, capacidades del trackpad, contadores de paquetes, toques y desplazamientos;
  - `trackpad gestos` / `trackpad relativo`;
  - `trackpad natural si|no` (desplazamiento al estilo del Mac), `trackpad toque si|no`, `trackpad velocidad 1-10`.
- **Seguridad**: si el trackpad no acepta el modo absoluto, o llegan bytes que no forman paquetes válidos durante 2 s, SOMA
  vuelve solo al modo relativo y lo cuenta (`vueltas automáticas` en `trackpad`). Un ratón PS/2 normal no se toca.

## Interruptores
- `soma.gestos=off`: modo relativo, como antes del parche.
- `trackpad relativo`: lo mismo, guardado en `/etc/trackpad.conf`.
- El modo seguro arranca sin gestos.

## Pruebas
- `tests/test_syn_167.c`: el reconocedor de gestos en el anfitrión (movimiento, toques, dos dedos, palma, TrackPoint, clic con
  dos dedos) y 200 000 paquetes aleatorios sin fallos.
- `tests/test_i8042_155.c` ampliado: el simulador del Synaptics comprueba la consulta de capacidades, el byte de modo `0xC5`
  con la tasa especial `0x14`, la activación del TrackPoint por el canal de paso y la vuelta al modo relativo.
- QEMU no emula un Synaptics: allí sigue el ratón PS/2 normal y el parche no cambia nada. Las pruebas reales son en el T480s.

## En el portátil
1. `trackpad`: debe decir `modo GESTOS`, `clickpad si`, `TrackPoint por pass-through si`.
2. Desplaza una web con dos dedos, toca con uno (clic) y con dos (menú contextual), mueve el TrackPoint.
3. Si algo va mal: `trackpad relativo` (o `soma.gestos=off` al arrancar) y mándame la salida de `trackpad`.

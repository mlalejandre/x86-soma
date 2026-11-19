# Parche 155 — Lenovo ThinkPad T480s: teclado y trackpad

Segundo parche de la serie T480s. Requiere el 154 (arranque y pantalla).

## Qué cambia

| Pieza | Antes | Ahora |
|---|---|---|
| Controlador PS/2 (i8042) | Se reprogramaba al abrir la GUI, sin mirar el estado del firmware | Se inicializa al arrancar con la secuencia de Linux (`i8042.c`): puertos apagados, buffer vacío, CTR reescrito, prueba del puerto auxiliar |
| Teclado | Dependía de cómo lo dejara el firmware (tras UEFI puede quedar sin escanear) | Se configura: juego de códigos 2 + traducción, LEDs, escaneo activado (como `atkbd`) |
| Trackpad | Ratón PS/2 genérico | Detecta el **Synaptics** (identificación `E8 00 ×4 + E9`, igual que `synaptics.c` de Linux) |
| Bytes del ratón | El manejador del teclado los descartaba si coincidían con una tecla, y el trackpad se desincronizaba | Un único servicio reparte cada byte: teclas al traductor, ratón a una cola de 512 bytes |
| Interrupciones | Solo IRQ 1; el ratón se leía por sondeo | IRQ 1 + IRQ 12, y además un respaldo desde el reloj cada 2 ms |
| Paquetes del ratón | Sin resincronización | Si un paquete se queda a medias más de 50 ms se descarta (como `psmouse`) |
| Distribución | US con ñ y tildes | **Española completa** en el T480s: AltGr (`@ # [ ] { } \ \| € ~ ¬`), teclas muertas `´ ¨ ` ^`, `ç`, `º ª`, `¡ ¿`, tecla `< >` |

### Trackpad y TrackPoint

- El trackpad queda en **modo relativo**, el de fábrica, igual que con el driver genérico de Windows o el `psmouse` de Linux sin Synaptics:
  - mover el dedo mueve el puntero;
  - un **toque** es un clic izquierdo;
  - el **TrackPoint** también mueve el puntero (el Synaptics mezcla sus paquetes con los suyos);
  - los **tres botones físicos** de arriba son izquierdo, central y derecho.
- **Desplazar:** mantén el **botón central** y mueve el TrackPoint (o el dedo) arriba o abajo. Es lo que hace Linux por defecto en los ThinkPad.
- **Aceleración:** según la velocidad, de 1x a unas 3x (×1,5 en pantallas WQHD), solo con el trackpad Synaptics. Con un ratón normal o en QEMU el movimiento es el mismo de antes.
- **Pendiente para un parche posterior:** desplazamiento con dos dedos, gestos y clic en la zona inferior del clickpad. Requieren el modo absoluto de Synaptics o RMI4 por SMBus.

### Teclado español

- **Cuándo se activa:** solo al arrancar desde el menú del T480s (opción `soma.maquina=t480s`).
- **Cambiarla sin reiniciar:** `teclado es` / `teclado us` en la shell.
- **Cambiarla desde GRUB:** pulsa `e` en el menú y añade `soma.teclado=us` (o `es`) a la línea `multiboot2`.
- **Atajos:** Ctrl + `+` / Ctrl + `-` cambian el tamaño del texto, también con las teclas `+` y `-` del teclado español.
- **Limitación conocida:** algunos signos (`€`, `à`, `â`…) llegan bien como UTF-8, pero la fuente de algunas partes de la GUI aún no los dibuja y salen como `?`.

### Diagnóstico

- **Al arrancar** se muestra una línea como esta:
  ```
  PS/2: teclado listo (id AB 41, juego 2 traducido) | puntero: trackpad Synaptics v8.x + TrackPoint (modo relativo)
  ```
- **La orden `teclado`** muestra:
  - el byte de control (CTR) que dejó el firmware y el que pone SOMA;
  - la identificación del teclado y del Synaptics;
  - los bytes recibidos de cada dispositivo.

  Si algo no responde en el portátil, envíame una foto de esa salida.

## Construir y grabar (macOS)

```sh
make clean
make docker-make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_ASYNC_IMAGES=1' DOCKER_TARGET=t480s-usb
```

Esto genera `build/usb/EFI/BOOT/BOOTX64.EFI` (GRUB, el menú y el núcleo en un solo archivo). Es el método que funciona
en el T480s:

```sh
diskutil list                                    # localiza el pendrive (p. ej. disk4)
diskutil eraseDisk FAT32 SOMA MBRFormat /dev/disk4
mkdir -p /Volumes/SOMA/EFI/BOOT
cp build/usb/EFI/BOOT/BOOTX64.EFI /Volumes/SOMA/EFI/BOOT/
diskutil eject /dev/disk4
```

Si el pendrive ya está en FAT32 con SOMA, basta con volver a copiar `BOOTX64.EFI` encima.

En el ThinkPad: **F12** → USB → «SOMA - Lenovo ThinkPad T480s». Si el USB no aparece en F12, entra en la BIOS (F1), pulsa
**F9** (*Setup Defaults*), vuelve a poner *Secure Boot* en *Disabled* y guarda con F10. Así se recuperan las entradas USB
de la lista de arranque.

## Pruebas realizadas

- **Simulador del controlador en el anfitrión** (`tests/test_i8042_155.c`, con ASan/UBSan). Emula el EC del ThinkPad con un
  Synaptics v8.1 y, en una segunda variante, un ratón IntelliMouse. Comprueba:
  - la secuencia de arranque y el CTR final;
  - la detección del trackpad, y que al Synaptics no se le envía la secuencia de la rueda;
  - el reparto de bytes cuando teclado y ratón llegan entremezclados;
  - que 90 bytes seguidos llegan sin pérdidas y en orden;
  - que una tecla pulsada durante una orden al ratón no se pierde.
- **QEMU q35 + UEFI y BIOS:**
  - el teclado escribe `hola "@éñn` (AltGr, tecla muerta y ñ);
  - el ratón mueve el puntero;
  - el `BOOTX64.EFI` de `make t480s-usb` arranca desde un pendrive FAT32 simulado.
- **`make run` de siempre:** teclado y ratón como antes, con la distribución de siempre.
- **Raspberry Pi 5 (aarch64):** compila sin avisos.

QEMU no emula un Synaptics, así que la ruta del trackpad real solo se ha probado con el simulador. La primera prueba en el
portátil es la de verdad.

## Revertir

`python3 parche_155_t480s_teclado_trackpad.py --undo` (antes de revertir el 154).

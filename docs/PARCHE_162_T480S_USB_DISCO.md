# Parche 162 · T480s: los archivos se guardan en el pendrive

Requiere el 159. Hasta ahora, en el portátil SOMA formateaba su sistema de archivos en RAM en cada arranque: todo lo que
escribías se perdía al apagar. Ahora el disco del sistema es una partición del propio pendrive, **SOMADATA**.

## Cómo queda el pendrive (lo hace `ejecutar.py -t t480s-usb`)
| Partición | Tamaño | Para qué |
|---|---|---|
| 1 · SOMA (FAT32) | 1 GB | El arranque (EFI/BOOT/BOOTX64.EFI). SOMA nunca escribe aquí. |
| 2 · SOMADATA | 4 GB | El disco de SOMA: archivos, diario, checkpoints, BIGFS. |
| 3 · DATOS (FAT32) | el resto | Para ti, desde el Mac. SOMA no la toca. |

En pendrives de menos de 7 GB: SOMA 512 MB + SOMADATA con el resto.

- **Pendrive nuevo**: pide `BORRAR` y lo prepara así.
- **Pendrive ya preparado**: solo copia el arranque; tus archivos de SOMA se conservan.
- **Pendrive del parche 155** (solo tiene SOMA): avisa de que falta SOMADATA y ofrece prepararlo (`BORRAR`) o seguir sin ella.
- Al conectarlo al Mac, macOS puede decir que no puede leer un disco: es SOMADATA (formato propio de SOMA). Pulsa
  **Ignorar**, nunca «Inicializar».

## Qué cambia en el núcleo
- **`src/arch/x86_64/usbdisk.c`** (nuevo): si no hay disco virtio (el portátil), arranca el USB antes que el sistema de
  archivos y busca en el MBR del pendrive la partición:
  - con su **marca** (primer sector: `SOMADATA-DISCO-1` + inicio y tamaño de la partición + firma del disco del MBR), o
  - **recién creada**: un FAT con la etiqueta `SOMADATA`. La reclama: pone a cero sus primeros 8 MiB, escribe la marca y
    la relee.
  - El disco de SOMA empieza 4 KiB después de la marca y se limita a 8 GiB.
- **Seguridad**:
  - cada petición se comprueba contra los límites de la partición dos veces antes de llegar al USB;
  - fuera de SOMADATA no se escribe nunca (ni el MBR, ni la partición de arranque, ni DATOS);
  - si el controlador USB se reinicia (enchufar o quitar algo), antes de la siguiente operación se relee la marca. Si el
    pendrive ya no es el mismo, no se lee ni se escribe nada hasta que vuelva el bueno, y entonces se recupera solo;
  - mientras falta, se reexplora el USB cada 10 s.
- **`virtio_blk.c`**: el «disco del sistema» se puede redirigir (`virtio_blk_set_alt`). SOMAFS, el diario, los
  checkpoints y BIGFS funcionan igual sin enterarse.
- **`xhci.c`**:
  - escritura sin vaciar la caché tras cada sector (el pendrive la vacía en las barreras del sistema de archivos, y un
    hilo cada 5 s si hay cambios);
  - cerrojo atómico del disco;
  - la reexploración espera a que termine la E/S en curso;
  - contador de generación;
  - arranque temprano sin mirar `/etc/nousb`.
- **Portada y `sysinfo`**: el disco y la red ahora son los de verdad (antes ponía siempre «RamFS (16 MiB virtio-blk)» y
  «10.0.2.15 (VirtIO-NET NAT)»).
- **Orden `disco`**: dónde se guardan los archivos, partición, estado (lectura/escritura, cambios pendientes) y
  contadores. `disco sync` vacía la caché del pendrive al momento.
- **Pruebas en QEMU**: `opt/soma/cmds` admite hasta 2 KiB de órdenes (antes 512 bytes: una lista más larga se perdía).

## Interruptores
- `soma.usbdisk=off` (o el modo seguro): como antes, todo en RAM.

## Pruebas realizadas
- **Anfitrión** (`tests/test_usbdisk_162.c`, ASan/UBSan):
  - MBR como el que deja `diskutil`: solo SOMADATA se reconoce;
  - se rechazan GPT, particiones fuera del disco, diminutas o extendidas;
  - la marca no se acepta con otra firma, otro inicio u otro tamaño;
  - 1 000 000 de peticiones aleatorias: ninguna sale de la partición y todas las válidas se aceptan;
  - 20 000 MBR mutados.
- **QEMU, pendrive de dos particiones por xHCI (UEFI)**:
  - primer arranque: reclama SOMADATA y formatea SOMAFS;
  - escribir un archivo, apagar, volver a arrancar: el archivo sigue ahí;
  - **la partición de arranque y el MBR quedan idénticos byte a byte**, y el archivo puesto desde el «Mac» se sigue
    leyendo.
- **Segundo pendrive enchufado en caliente** (también con su SOMADATA) mientras se escribe: SOMA sigue en el suyo; el
  otro queda idéntico byte a byte.
- **Cambio de pendrive** (quitar el del sistema y poner otro): escrituras rechazadas, el otro intacto. Al volver a
  poner el bueno: «ha vuelto», y las escrituras siguen.
- **Tres cortes de corriente** en mitad de escrituras: el sistema de archivos monta y cada archivo tiene una versión
  completa.
- QEMU normal con disco virtio: sin cambios. `ejecutar.py`: los cinco casos de preparación del pendrive, simulando
  `diskutil`. Raspberry Pi 5: compila.

## En el portátil
1. `python3 ejecutar.py -t t480s-usb` → como tu pendrive es del 155, te avisará: escribe `BORRAR` para añadir
   SOMADATA. Si tienes algo tuyo en el pendrive, cópialo antes.
2. Arranca. En la pantalla de arranque: `DISCO USB: particion SOMADATA nueva: preparandola` y después
   `disco del sistema en la particion 2`.
3. `write /prueba.txt hola`, apaga, vuelve a arrancar y `cat /prueba.txt`. Envíame una foto de `disco`.
4. Si dice que no encuentra el disco USB, foto de `usb` y de `disco`: el controlador USB del portátil puede necesitar
   ajustes (lo veremos con esa foto).

## Revertir
`python3 parche_162_t480s_usb_disco.py --undo` y `make clean`. El pendrive no se toca al revertir.
